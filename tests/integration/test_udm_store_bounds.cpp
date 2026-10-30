// REVIEW-5ae0ef1 SUS-02 (ADR-0535): the UDM's in-memory stores are bounded per store and per
// access-token sub, and subscriptions carry an expiry the UDM confirms (TS 29.503 6.1.6.2.3,
// 6.4.6.2.6) and drops once passed. Real nrf + udm processes over TLS 1.3 + mTLS HTTP/2 with real
// NRF tokens; the bounds come from config/udm.json, narrowed per test through its env overrides.

#include "sbi_core/datetime.hpp"
#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>

#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;
using std::chrono::hours;
using std::chrono::seconds;
using std::chrono::system_clock;

constexpr const char* kSdmBase = "https://127.0.0.1:7780/nudm-sdm/v2/imsi-999700000000011";
constexpr const char* kEeBase = "https://127.0.0.1:7780/nudm-ee/v1/imsi-999700000000011";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

bool wait_reachable(sbi_core::http2::Client& client, const std::string& url, int attempts) {
    for (int i = 0; i < attempts; ++i) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = url;
        if (client.send(req).has_value()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

std::string fetch_token(const std::string& scope) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body =
        "grant_type=client_credentials&nfInstanceId=" + std::string(nf_test::kTestAfInstanceId) +
        "&scope=" + scope + "&targetNfType=UDM";
    auto resp = nf_test::send_token_request(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

struct Lab {
    nf_test::SpawnedProcess nrf;
    nf_test::SpawnedProcess udm;
};

// Spawns nrf + udm with `env` (name, value) applied to the udm only.
Lab spawn(std::initializer_list<std::pair<const char*, const char*>> env) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    for (const auto& [name, value] : env) {
        setenv(name, value, 1);
    }
    nf_test::SpawnedProcess udm(UDM_PATH);
    for (const auto& [name, value] : env) {
        unsetenv(name);
    }
    return Lab{std::move(nrf), std::move(udm)};
}

sbi_core::http2::ClientRequest
request(const std::string& method, const std::string& url, const std::string& token, json body) {
    sbi_core::http2::ClientRequest req;
    req.method = method;
    req.url = url;
    req.headers.emplace("authorization", "Bearer " + token);
    if (!body.is_null()) {
        req.headers.emplace("content-type",
                            method == "PATCH" ? "application/merge-patch+json"
                                              : "application/json");
        req.body = body.dump();
    }
    return req;
}

json sdm_subscription() {
    return json{{"nfInstanceId", "00000000-0000-4000-8000-000000000bbb"},
                {"callbackReference", "https://example.com/cb"},
                {"monitoredResourceUris", json::array({"https://example.com/res"})}};
}

std::string id_of(const sbi_core::http2::ClientResponse& resp) {
    const auto loc = resp.headers.find("location");
    return loc == resp.headers.end() ? "" : loc->second.substr(loc->second.rfind('/') + 1);
}

} // namespace

TEST(UdmStoreBounds, OneConsumerGetsInsufficientResourcesBeyondItsShare) {
    auto lab = spawn({{"UDM_STORE_MAX_ENTRIES_PER_CONSUMER", "1"}});
    ASSERT_GT(lab.udm.pid(), 0);
    auto client = make_client();
    const std::string url = std::string(kSdmBase) + "/sdm-subscriptions";
    ASSERT_TRUE(wait_reachable(client, url, 50)) << "udm never reachable";
    const auto token = fetch_token("nudm-sdm");
    ASSERT_FALSE(token.empty());

    auto first = client.send(request("POST", url, token, sdm_subscription()));
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->status, 201) << first->body;

    auto second = client.send(request("POST", url, token, sdm_subscription()));
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->status, 500) << second->body;
    EXPECT_EQ(json::parse(second->body).value("cause", ""), "INSUFFICIENT_RESOURCES")
        << second->body;

    // Unsubscribing frees the consumer's share.
    auto del = client.send(request("DELETE", url + "/" + id_of(*first), token, json()));
    ASSERT_TRUE(del.has_value());
    EXPECT_EQ(del->status, 204) << del->body;
    auto third = client.send(request("POST", url, token, sdm_subscription()));
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(third->status, 201) << third->body;
}

TEST(UdmStoreBounds, TheUdmConfirmsAnExpiryNoLaterThanItsMaximumLifetime) {
    auto lab = spawn({});
    ASSERT_GT(lab.udm.pid(), 0);
    auto client = make_client();
    const std::string url = std::string(kSdmBase) + "/sdm-subscriptions";
    ASSERT_TRUE(wait_reachable(client, url, 50)) << "udm never reachable";
    const auto token = fetch_token("nudm-sdm");
    ASSERT_FALSE(token.empty());
    // config/udm.json: subscription_max_lifetime_seconds 86400; the UDM spreads the times it sets
    // over the last tenth of that.
    const auto within_policy = [](const json& value) {
        const auto t = sbi_core::parse_rfc3339(value.get<std::string>());
        const auto now = system_clock::now();
        return t.has_value() && *t <= now + hours(24) + seconds(5) &&
               *t >= now + hours(24) - seconds(hours(24)) / 10 - seconds(5);
    };

    // Proposed too far ahead: shortened.
    auto body = sdm_subscription();
    body["expires"] = sbi_core::format_rfc3339(system_clock::now() + hours(24 * 10));
    auto later = client.send(request("POST", url, token, body));
    ASSERT_TRUE(later.has_value());
    ASSERT_EQ(later->status, 201) << later->body;
    EXPECT_TRUE(within_policy(json::parse(later->body).at("expires"))) << later->body;

    // None proposed: the UDM sets one.
    auto none = client.send(request("POST", url, token, sdm_subscription()));
    ASSERT_TRUE(none.has_value());
    ASSERT_EQ(none->status, 201) << none->body;
    EXPECT_TRUE(within_policy(json::parse(none->body).at("expires"))) << none->body;

    // Not an RFC 3339 date-time: TS 29.500 OPTIONAL_IE_INCORRECT.
    body["expires"] = "next tuesday";
    auto bad = client.send(request("POST", url, token, body));
    ASSERT_TRUE(bad.has_value());
    EXPECT_EQ(bad->status, 400) << bad->body;
    EXPECT_EQ(json::parse(bad->body).value("cause", ""), "OPTIONAL_IE_INCORRECT") << bad->body;

    // EE: the confirmed time is reportingOptions.expiry.
    const auto ee_token = fetch_token("nudm-ee");
    ASSERT_FALSE(ee_token.empty());
    auto ee = client.send(request(
        "POST",
        std::string(kEeBase) + "/ee-subscriptions",
        ee_token,
        json{{"callbackReference", "https://example.com/ee-cb"},
             {"monitoringConfigurations",
              {{"1", {{"eventType", "LOSS_OF_CONNECTIVITY"}, {"immediateFlag", false}}}}}}));
    ASSERT_TRUE(ee.has_value());
    ASSERT_EQ(ee->status, 201) << ee->body;
    EXPECT_TRUE(within_policy(
        json::parse(ee->body).at("eeSubscription").at("reportingOptions").at("expiry")))
        << ee->body;
}

TEST(UdmStoreBounds, AnEeSubscriptionWhoseOnlyReportIsInTheResponseExpiresAtOnce) {
    auto lab = spawn({});
    ASSERT_GT(lab.udm.pid(), 0);
    auto client = make_client();
    const std::string url = std::string(kEeBase) + "/ee-subscriptions";
    ASSERT_TRUE(wait_reachable(client, url, 50)) << "udm never reachable";

    // A serving AMF in a home PLMN (config/udm.json home_plmns 999/70), so ROAMING_STATUS with
    // immediateFlag has a report to return.
    const auto uecm_token = fetch_token("nudm-uecm");
    ASSERT_FALSE(uecm_token.empty());
    auto amf = client.send(
        request("PUT",
                "https://127.0.0.1:7780/nudm-uecm/v1/imsi-999700000000011/registrations/"
                "amf-3gpp-access",
                uecm_token,
                json{{"amfInstanceId", "00000000-0000-4000-8000-000000000aaa"},
                     {"deregCallbackUri", "https://example.com/dereg"},
                     {"guami", {{"plmnId", {{"mcc", "999"}, {"mnc", "70"}}}, {"amfId", "ABCDEF"}}},
                     {"ratType", "NR"}}));
    ASSERT_TRUE(amf.has_value());
    ASSERT_EQ(amf->status, 201) << amf->body;

    const auto token = fetch_token("nudm-ee");
    ASSERT_FALSE(token.empty());
    auto ee = client.send(
        request("POST",
                url,
                token,
                json{{"callbackReference", "https://example.com/ee-cb"},
                     {"monitoringConfigurations",
                      {{"1", {{"eventType", "ROAMING_STATUS"}, {"immediateFlag", true}}}}},
                     {"reportingOptions", {{"maxNumOfReports", 1}}}}));
    ASSERT_TRUE(ee.has_value());
    ASSERT_EQ(ee->status, 201) << ee->body;
    const auto created = json::parse(ee->body);
    ASSERT_TRUE(created.contains("eventReports")) << ee->body;
    // TS 29.503 6.4.6.2.6: "an immediate timestamp".
    const auto expiry = sbi_core::parse_rfc3339(
        created.at("eeSubscription").at("reportingOptions").at("expiry").get<std::string>());
    ASSERT_TRUE(expiry.has_value()) << ee->body;
    EXPECT_LE(*expiry, system_clock::now());
    EXPECT_GE(*expiry, system_clock::now() - seconds(5));
}

TEST(UdmStoreBounds, AnExpiredSubscriptionIsRemoved) {
    auto lab = spawn({{"UDM_STORE_SWEEP_INTERVAL_SECONDS", "1"}});
    ASSERT_GT(lab.udm.pid(), 0);
    auto client = make_client();
    const std::string url = std::string(kSdmBase) + "/sdm-subscriptions";
    ASSERT_TRUE(wait_reachable(client, url, 50)) << "udm never reachable";
    const auto token = fetch_token("nudm-sdm");
    ASSERT_FALSE(token.empty());

    auto body = sdm_subscription();
    const auto expires = sbi_core::format_rfc3339(system_clock::now() + seconds(2));
    body["expires"] = expires;
    auto created = client.send(request("POST", url, token, body));
    ASSERT_TRUE(created.has_value());
    ASSERT_EQ(created->status, 201) << created->body;
    EXPECT_EQ(json::parse(created->body).value("expires", ""), expires)
        << "a proposal inside the lifetime comes back unchanged";
    const auto item = url + "/" + id_of(*created);

    // An empty merge-patch changes nothing: 200 while the subscription exists, 404 once swept.
    long status = 0;
    for (int i = 0; i < 80 && status != 404; ++i) {
        auto probe = client.send(request("PATCH", item, token, json::object()));
        ASSERT_TRUE(probe.has_value());
        status = probe->status;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EXPECT_EQ(status, 404) << "the subscription outlived its expiry";
}
