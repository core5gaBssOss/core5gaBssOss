// REVIEW-5ae0ef1 SUS-02 (ADR-0564): the UDR's subscription tables are bounded per table and per
// access-token sub, with counts kept in its PostgreSQL database, and the subscriptions the UDR
// owns carry an expiry it confirms (TS 29.504 5.2.2.6.2) and deletes once passed. Real nrf + udr
// processes over TLS 1.3 + mTLS HTTP/2 with real NRF tokens; the bounds come from config/udr.json,
// narrowed per test through its env overrides. The database outlives a test, so each test that
// counts uses a token for a fresh consumer, and removes what it created.

#include "sbi_core/datetime.hpp"
#include "sbi_core/http2_client.hpp"
#include "sbi_core/uuid.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <initializer_list>
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

constexpr const char* kSubsToNotify = "https://127.0.0.1:7781/nudr-dr/v2/subscription-data/"
                                      "subs-to-notify";
constexpr const char* kAppDataSubs = "https://127.0.0.1:7781/nudr-dr/v2/application-data/"
                                     "subs-to-notify";

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

// A nudr-dr token whose sub is `nf_instance_id` (registered with the NRF as an AF first).
std::string fetch_token(const std::string& nf_instance_id) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body = "grant_type=client_credentials&nfInstanceId=" + nf_instance_id +
               "&scope=nudr-dr&targetNfType=UDR";
    auto resp = nf_test::send_token_request_as(req, nf_instance_id);
    for (int attempt = 0; attempt < 50 && resp.has_value() && resp->status == 400; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // the UDR's NFRegister
        resp = nf_test::send_token_request_as(req, nf_instance_id);
    }
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

struct Lab {
    nf_test::SpawnedProcess nrf;
    nf_test::SpawnedProcess udr;
};

// Spawns nrf + udr with `env` (name, value) applied to the udr only.
Lab spawn(std::initializer_list<std::pair<const char*, const char*>> env) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    for (const auto& [name, value] : env) {
        setenv(name, value, 1);
    }
    nf_test::SpawnedProcess udr(UDR_PATH);
    for (const auto& [name, value] : env) {
        unsetenv(name);
    }
    return Lab{std::move(nrf), std::move(udr)};
}

sbi_core::http2::ClientRequest request(const std::string& method,
                                       const std::string& url,
                                       const std::string& token,
                                       const json& body,
                                       const char* content_type = "application/json") {
    sbi_core::http2::ClientRequest req;
    req.method = method;
    req.url = url;
    req.headers.emplace("authorization", "Bearer " + token);
    if (!body.is_null()) {
        req.headers.emplace("content-type", content_type);
        req.body = body.dump();
    }
    return req;
}

// TS29505 SubscriptionDataSubscriptions: monitoredResourceUris and callbackReference required.
json subscription() {
    return json{{"callbackReference", "https://example.com/udr-cb"},
                {"monitoredResourceUris",
                 json::array({"https://127.0.0.1:7781/nudr-dr/v2/subscription-data/"
                              "imsi-999700000000001/context-data/amf-3gpp-access"})}};
}

std::string id_of(const sbi_core::http2::ClientResponse& resp) {
    const auto loc = resp.headers.find("location");
    return loc == resp.headers.end() ? "" : loc->second.substr(loc->second.rfind('/') + 1);
}

std::string cause_of(const sbi_core::http2::ClientResponse& resp) {
    return json::parse(resp.body).value("cause", "");
}

// config/udr.json: subscription_max_lifetime_seconds 86400; the UDR spreads the times it sets
// over the last tenth of that.
bool within_policy(const json& value) {
    const auto t = sbi_core::parse_rfc3339(value.get<std::string>());
    const auto now = system_clock::now();
    return t.has_value() && *t <= now + hours(24) + seconds(5) &&
           *t >= now + hours(24) - seconds(hours(24)) / 10 - seconds(5);
}

} // namespace

TEST(UdrStoreBounds, AConsumerOverItsShareGets403CreationNotAllowed) {
    auto lab = spawn({{"UDR_STORE_MAX_ENTRIES_PER_CONSUMER", "1"}});
    ASSERT_GT(lab.udr.pid(), 0);
    auto client = make_client();
    ASSERT_TRUE(wait_reachable(client, kSubsToNotify, 100)) << "udr never reachable";
    const auto token = fetch_token(sbi_core::generate_uuid_v4());
    ASSERT_FALSE(token.empty());

    auto first = client.send(request("POST", kSubsToNotify, token, subscription()));
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->status, 201) << first->body;

    auto second = client.send(request("POST", kSubsToNotify, token, subscription()));
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->status, 403) << second->body;
    EXPECT_EQ(cause_of(*second), "CREATION_NOT_ALLOWED") << second->body;

    // Deleting frees the consumer's share.
    const auto first_url = std::string(kSubsToNotify) + "/" + id_of(*first);
    auto del = client.send(request("DELETE", first_url, token, json()));
    ASSERT_TRUE(del.has_value());
    EXPECT_EQ(del->status, 204) << del->body;
    auto third = client.send(request("POST", kSubsToNotify, token, subscription()));
    ASSERT_TRUE(third.has_value());
    EXPECT_EQ(third->status, 201) << third->body;
    client.send(request("DELETE", std::string(kSubsToNotify) + "/" + id_of(*third), token, json()));
}

TEST(UdrStoreBounds, AReplaceIsNeverRefusedAndANewKeyIs) {
    auto lab = spawn({{"UDR_STORE_MAX_ENTRIES_PER_CONSUMER", "1"}});
    ASSERT_GT(lab.udr.pid(), 0);
    auto client = make_client();
    ASSERT_TRUE(wait_reachable(client, kAppDataSubs, 100)) << "udr never reachable";
    const auto token = fetch_token(sbi_core::generate_uuid_v4());
    ASSERT_FALSE(token.empty());
    // TS29519 ApplicationDataSubs: notificationUri required. This PUT stores under the caller's id.
    const json body{{"notificationUri", "https://example.com/app-cb"}};
    const auto a = std::string(kAppDataSubs) + "/" + sbi_core::generate_uuid_v4();
    const auto b = std::string(kAppDataSubs) + "/" + sbi_core::generate_uuid_v4();

    auto created = client.send(request("PUT", a, token, body));
    ASSERT_TRUE(created.has_value());
    ASSERT_EQ(created->status, 200) << created->body;
    auto replaced = client.send(request("PUT", a, token, body));
    ASSERT_TRUE(replaced.has_value());
    EXPECT_EQ(replaced->status, 200) << "a replace counts nothing: " << replaced->body;
    auto refused = client.send(request("PUT", b, token, body));
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->status, 403) << refused->body;
    EXPECT_EQ(cause_of(*refused), "CREATION_NOT_ALLOWED") << refused->body;
    client.send(request("DELETE", a, token, json()));
}

TEST(UdrStoreBounds, AFullStoreGets500InsufficientResources) {
    auto lab = spawn({{"UDR_STORE_MAX_ENTRIES", "0"}});
    ASSERT_GT(lab.udr.pid(), 0);
    auto client = make_client();
    ASSERT_TRUE(wait_reachable(client, kSubsToNotify, 100)) << "udr never reachable";
    const auto token = fetch_token(sbi_core::generate_uuid_v4());
    ASSERT_FALSE(token.empty());

    auto resp = client.send(request("POST", kSubsToNotify, token, subscription()));
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->status, 500) << resp->body;
    EXPECT_EQ(cause_of(*resp), "INSUFFICIENT_RESOURCES") << resp->body;
}

TEST(UdrStoreBounds, TheUdrConfirmsAnExpiryNoLaterThanItsMaximumLifetime) {
    auto lab = spawn({});
    ASSERT_GT(lab.udr.pid(), 0);
    auto client = make_client();
    ASSERT_TRUE(wait_reachable(client, kSubsToNotify, 100)) << "udr never reachable";
    const auto token = fetch_token(sbi_core::generate_uuid_v4());
    ASSERT_FALSE(token.empty());

    // Proposed too far ahead: shortened (TS 29.504 5.2.2.6.2: "less than or equal to").
    auto body = subscription();
    body["expiry"] = sbi_core::format_rfc3339(system_clock::now() + hours(24 * 10));
    auto later = client.send(request("POST", kSubsToNotify, token, body));
    ASSERT_TRUE(later.has_value());
    ASSERT_EQ(later->status, 201) << later->body;
    EXPECT_TRUE(within_policy(json::parse(later->body).at("expiry"))) << later->body;
    const auto later_url = std::string(kSubsToNotify) + "/" + id_of(*later);

    // A modify that pushes it out again is re-confirmed.
    const json patch =
        json::array({{{"op", "replace"},
                      {"path", "/expiry"},
                      {"value", sbi_core::format_rfc3339(system_clock::now() + hours(24 * 30))}}});
    auto patched =
        client.send(request("PATCH", later_url, token, patch, "application/json-patch+json"));
    ASSERT_TRUE(patched.has_value());
    EXPECT_EQ(patched->status, 204) << patched->body;
    auto got = client.send(request("GET", later_url, token, json()));
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ(got->status, 200) << got->body;
    EXPECT_TRUE(within_policy(json::parse(got->body).at("expiry"))) << got->body;

    // None proposed: the UDR sets one.
    auto none = client.send(request("POST", kSubsToNotify, token, subscription()));
    ASSERT_TRUE(none.has_value());
    ASSERT_EQ(none->status, 201) << none->body;
    EXPECT_TRUE(within_policy(json::parse(none->body).at("expiry"))) << none->body;

    // Not an RFC 3339 date-time: TS 29.500 OPTIONAL_IE_INCORRECT.
    body["expiry"] = "next tuesday";
    auto bad = client.send(request("POST", kSubsToNotify, token, body));
    ASSERT_TRUE(bad.has_value());
    EXPECT_EQ(bad->status, 400) << bad->body;
    EXPECT_EQ(cause_of(*bad), "OPTIONAL_IE_INCORRECT") << bad->body;

    client.send(request("DELETE", later_url, token, json()));
    client.send(request("DELETE", std::string(kSubsToNotify) + "/" + id_of(*none), token, json()));
}

TEST(UdrStoreBounds, AnExpiredSubscriptionIsDeleted) {
    auto lab = spawn({{"UDR_STORE_SWEEP_INTERVAL_SECONDS", "1"}});
    ASSERT_GT(lab.udr.pid(), 0);
    auto client = make_client();
    ASSERT_TRUE(wait_reachable(client, kSubsToNotify, 100)) << "udr never reachable";
    const auto token = fetch_token(sbi_core::generate_uuid_v4());
    ASSERT_FALSE(token.empty());

    auto body = subscription();
    const auto expiry = sbi_core::format_rfc3339(system_clock::now() + seconds(2));
    body["expiry"] = expiry;
    auto created = client.send(request("POST", kSubsToNotify, token, body));
    ASSERT_TRUE(created.has_value());
    ASSERT_EQ(created->status, 201) << created->body;
    EXPECT_EQ(json::parse(created->body).value("expiry", ""), expiry)
        << "a proposal inside the lifetime comes back unchanged";
    const auto url = std::string(kSubsToNotify) + "/" + id_of(*created);

    long status = 0;
    for (int i = 0; i < 80 && status != 404; ++i) {
        auto probe = client.send(request("GET", url, token, json()));
        ASSERT_TRUE(probe.has_value());
        status = probe->status;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EXPECT_EQ(status, 404) << "the subscription outlived its expiry";
}
