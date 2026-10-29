// REVIEW-5ae0ef1 SPEC-07 (ADR-0533): a PUT on the NEF's northbound resources only replaces. On
// these paths the YAML answers PUT with 200/204 and has no 201, so a PUT on an id no POST created
// is 404 and creates nothing, and a PUT on an existing resource is 200 with the replaced
// representation. Two routes, one per 404 cause:
// - TS29122_PfdManagement "Individual PFD Management Transaction": no API-specific or TS 29.500
//   cause fits a missing transaction, so the ProblemDetails carries none.
// - TS29522_TimeSyncExposure "Individual Time Synchronization Exposure Subscription":
//   SUBSCRIPTION_NOT_FOUND (TS 29.500 Table 5.2.7.2-1, applicable via TS 29.522 5.15.6.1 ->
//   TS 29.122 5.2.6).
// Drives nrf and nef as real processes over TLS 1.3 + mTLS HTTP/2 with real NRF tokens.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <utility>

#include "location_path.hpp"
#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

constexpr const char* kNefBase = "https://127.0.0.1:7790";
constexpr const char* kScsAsId = "scs-spec07";
constexpr const char* kAfId = "af-spec07";

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

struct Duo {
    nf_test::SpawnedProcess nrf;
    nf_test::SpawnedProcess nef;
};

Duo spawn_all() {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    nf_test::SpawnedProcess nef(NEF_PATH);
    return Duo{std::move(nrf), std::move(nef)};
}

sbi_core::http2::ClientRequest
json_request(const std::string& method, const std::string& url, const json& body) {
    sbi_core::http2::ClientRequest req;
    req.method = method;
    req.url = url;
    req.headers.emplace("content-type", "application/json");
    req.body = body.dump();
    return req;
}

// POSTs `body` to the collection and returns the created resource's URL ("" on failure).
std::string
create(sbi_core::http2::Client& client, const std::string& collection, const json& body) {
    auto resp = client.send(json_request("POST", collection, body));
    if (!resp.has_value() || resp->status != 201) {
        return "";
    }
    const auto location = resp->headers.find("location");
    if (location == resp->headers.end()) {
        return "";
    }
    return std::string(kNefBase) + nf_test::location_path(location->second);
}

json pfd_transaction(const std::string& pfd_id, const std::string& url) {
    return json{{"pfdDatas",
                 {{"app-spec07",
                   {{"externalAppId", "app-spec07"},
                    {"pfds", {{pfd_id, {{"pfdId", pfd_id}, {"urls", json::array({url})}}}}}}}}}};
}

json time_sync_subscription(const std::string& notif_id) {
    return json{{"anyUeInd", true},
                {"subsNotifUri", "https://af.example.com/time-sync-notify"},
                {"subsNotifId", notif_id},
                {"subscribedEvents", json::array({"AVAILABILITY_FOR_TIME_SYNC_SERVICE"})}};
}

} // namespace

TEST(NefPutReplacesOnly, PfdTransactionPutOnUnknownIdIs404AndCreatesNothing) {
    auto d = spawn_all();
    ASSERT_GT(d.nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string collection =
        std::string(kNefBase) + "/3gpp-pfd-management/v1/" + kScsAsId + "/transactions";
    ASSERT_TRUE(wait_reachable(client, collection, 200)) << "nef never reachable";

    const std::string unknown = collection + "/never-created";
    auto put_resp = client.send(
        json_request("PUT", unknown, pfd_transaction("pfd-1", "https://app.example.com/a")));
    ASSERT_TRUE(put_resp.has_value());
    EXPECT_EQ(put_resp->status, 404) << put_resp->body;
    const auto content_type = put_resp->headers.find("content-type");
    ASSERT_NE(content_type, put_resp->headers.end());
    EXPECT_EQ(content_type->second, "application/problem+json");
    const auto problem = json::parse(put_resp->body);
    EXPECT_EQ(problem.value("status", 0), 404);
    EXPECT_FALSE(problem.contains("cause"))
        << "TS 29.122 / TS 29.500 define no cause for a missing PFD transaction: "
        << put_resp->body;

    // The PUT created nothing under the id the AF chose.
    sbi_core::http2::ClientRequest get_req;
    get_req.method = "GET";
    get_req.url = unknown;
    auto get_resp = client.send(get_req);
    ASSERT_TRUE(get_resp.has_value());
    EXPECT_EQ(get_resp->status, 404) << get_resp->body;
}

TEST(NefPutReplacesOnly, PfdTransactionPutOnExistingIdIs200WithTheReplacement) {
    auto d = spawn_all();
    ASSERT_GT(d.nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string collection =
        std::string(kNefBase) + "/3gpp-pfd-management/v1/" + kScsAsId + "/transactions";
    ASSERT_TRUE(wait_reachable(client, collection, 200)) << "nef never reachable";

    const std::string url =
        create(client, collection, pfd_transaction("pfd-1", "https://app.example.com/a"));
    ASSERT_FALSE(url.empty()) << "POST did not create the transaction";

    const json replacement = pfd_transaction("pfd-2", "https://app.example.com/b");
    auto put_resp = client.send(json_request("PUT", url, replacement));
    ASSERT_TRUE(put_resp.has_value());
    EXPECT_EQ(put_resp->status, 200) << put_resp->body;
    const auto replaced = json::parse(put_resp->body);
    ASSERT_TRUE(replaced.contains("pfdDatas")) << put_resp->body;
    EXPECT_TRUE(replaced["pfdDatas"]["app-spec07"]["pfds"].contains("pfd-2")) << put_resp->body;
    EXPECT_FALSE(replaced["pfdDatas"]["app-spec07"]["pfds"].contains("pfd-1")) << put_resp->body;

    sbi_core::http2::ClientRequest get_req;
    get_req.method = "GET";
    get_req.url = url;
    auto get_resp = client.send(get_req);
    ASSERT_TRUE(get_resp.has_value());
    ASSERT_EQ(get_resp->status, 200) << get_resp->body;
    EXPECT_TRUE(json::parse(get_resp->body)["pfdDatas"]["app-spec07"]["pfds"].contains("pfd-2"))
        << get_resp->body;
}

TEST(NefPutReplacesOnly, TimeSyncSubscriptionPutOnUnknownIdIs404SubscriptionNotFound) {
    auto d = spawn_all();
    ASSERT_GT(d.nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string collection =
        std::string(kNefBase) + "/3gpp-time-sync/v1/" + kAfId + "/subscriptions";
    ASSERT_TRUE(wait_reachable(client, collection, 200)) << "nef never reachable";

    const std::string unknown = collection + "/never-created";
    auto put_resp = client.send(json_request("PUT", unknown, time_sync_subscription("notif-1")));
    ASSERT_TRUE(put_resp.has_value());
    EXPECT_EQ(put_resp->status, 404) << put_resp->body;
    const auto content_type = put_resp->headers.find("content-type");
    ASSERT_NE(content_type, put_resp->headers.end());
    EXPECT_EQ(content_type->second, "application/problem+json");
    const auto problem = json::parse(put_resp->body);
    EXPECT_EQ(problem.value("status", 0), 404);
    EXPECT_EQ(problem.value("cause", ""), "SUBSCRIPTION_NOT_FOUND") << put_resp->body;

    sbi_core::http2::ClientRequest get_req;
    get_req.method = "GET";
    get_req.url = unknown;
    auto get_resp = client.send(get_req);
    ASSERT_TRUE(get_resp.has_value());
    EXPECT_EQ(get_resp->status, 404) << get_resp->body;
}

TEST(NefPutReplacesOnly, TimeSyncSubscriptionPutOnExistingIdIs200WithTheReplacement) {
    auto d = spawn_all();
    ASSERT_GT(d.nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string collection =
        std::string(kNefBase) + "/3gpp-time-sync/v1/" + kAfId + "/subscriptions";
    ASSERT_TRUE(wait_reachable(client, collection, 200)) << "nef never reachable";

    const std::string url = create(client, collection, time_sync_subscription("notif-1"));
    ASSERT_FALSE(url.empty()) << "POST did not create the subscription";

    auto put_resp = client.send(json_request("PUT", url, time_sync_subscription("notif-2")));
    ASSERT_TRUE(put_resp.has_value());
    EXPECT_EQ(put_resp->status, 200) << put_resp->body;
    EXPECT_EQ(json::parse(put_resp->body).value("subsNotifId", ""), "notif-2") << put_resp->body;

    sbi_core::http2::ClientRequest get_req;
    get_req.method = "GET";
    get_req.url = url;
    auto get_resp = client.send(get_req);
    ASSERT_TRUE(get_resp.has_value());
    ASSERT_EQ(get_resp->status, 200) << get_resp->body;
    EXPECT_EQ(json::parse(get_resp->body).value("subsNotifId", ""), "notif-2") << get_resp->body;
}
