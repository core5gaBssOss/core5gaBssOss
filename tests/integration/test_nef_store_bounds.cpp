// REVIEW-5ae0ef1 SUS-02 (ADR-0536): the NEF's in-memory stores are bounded per store and per
// access-token sub. A consumer over its share of a TS 29.122 Network Status Reporting store gets
// the API's own 403 QUOTA_EXCEEDED (TS 29.122 4.4.8.2, Table 5.9.5.3-1); on an API that defines no
// such cause (TS 29.522 TimeSyncExposure) and for a full store, 500 INSUFFICIENT_RESOURCES
// (TS 29.500 Table 5.2.7.2-1, applicable via TS 29.122 5.2.6). Deleting a resource frees its slot.
// Real nrf + nef processes over TLS 1.3 + mTLS HTTP/2 with real NRF tokens; the bounds come from
// config/nef.json, narrowed per test through its env overrides.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <initializer_list>
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

// Spawns nrf + nef with `env` (name, value) applied to the nef only.
Duo spawn(std::initializer_list<std::pair<const char*, const char*>> env) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    for (const auto& [name, value] : env) {
        setenv(name, value, 1);
    }
    nf_test::SpawnedProcess nef(NEF_PATH);
    for (const auto& [name, value] : env) {
        unsetenv(name);
    }
    return Duo{std::move(nrf), std::move(nef)};
}

tl::expected<sbi_core::http2::ClientResponse, std::string>
post(sbi_core::http2::Client& client, const std::string& url, const json& body) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = url;
    req.headers.emplace("content-type", "application/json");
    req.body = body.dump();
    return client.send(req);
}

// TS29122_ReportingNetworkStatus NetworkStatusReportingSubscription: notificationDestination and
// locationArea are its required attributes.
json net_status_subscription() {
    return json{{"notificationDestination", "https://scs.example.com/net-status-notify"},
                {"locationArea", {{"cellIds", json::array({"0010100000001"})}}}};
}

json time_sync_subscription() {
    return json{{"anyUeInd", true},
                {"subsNotifUri", "https://af.example.com/time-sync-notify"},
                {"subsNotifId", "notif-sus02"},
                {"subscribedEvents", json::array({"AVAILABILITY_FOR_TIME_SYNC_SERVICE"})}};
}

void expect_problem(const tl::expected<sbi_core::http2::ClientResponse, std::string>& resp,
                    int status,
                    const std::string& cause) {
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->status, status) << resp->body;
    const auto content_type = resp->headers.find("content-type");
    ASSERT_NE(content_type, resp->headers.end());
    EXPECT_EQ(content_type->second, "application/problem+json");
    EXPECT_EQ(json::parse(resp->body).value("cause", ""), cause) << resp->body;
}

} // namespace

TEST(NefStoreBounds, AnScsAsOverItsShareOfNetworkStatusSubscriptionsGetsQuotaExceeded) {
    auto d = spawn({{"NEF_STORE_MAX_ENTRIES_PER_CONSUMER", "1"}});
    ASSERT_GT(d.nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string collection =
        std::string(kNefBase) + "/3gpp-net-stat-report/v1/scs-sus02/subscriptions";
    ASSERT_TRUE(wait_reachable(client, collection, 200)) << "nef never reachable";

    const auto first = post(client, collection, net_status_subscription());
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->status, 201) << first->body;
    expect_problem(post(client, collection, net_status_subscription()), 403, "QUOTA_EXCEEDED");

    // Deleting the subscription frees the share.
    const auto location = first->headers.find("location");
    ASSERT_NE(location, first->headers.end());
    sbi_core::http2::ClientRequest del;
    del.method = "DELETE";
    del.url = std::string(kNefBase) + nf_test::location_path(location->second);
    const auto deleted = client.send(del);
    ASSERT_TRUE(deleted.has_value());
    ASSERT_EQ(deleted->status, 204) << deleted->body;
    const auto again = post(client, collection, net_status_subscription());
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->status, 201) << again->body;
}

TEST(NefStoreBounds, AnApiWithoutAQuotaCauseAnswersInsufficientResources) {
    auto d = spawn({{"NEF_STORE_MAX_ENTRIES_PER_CONSUMER", "1"}});
    ASSERT_GT(d.nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string collection =
        std::string(kNefBase) + "/3gpp-time-sync/v1/af-sus02/subscriptions";
    ASSERT_TRUE(wait_reachable(client, collection, 200)) << "nef never reachable";

    const auto first = post(client, collection, time_sync_subscription());
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->status, 201) << first->body;
    expect_problem(
        post(client, collection, time_sync_subscription()), 500, "INSUFFICIENT_RESOURCES");
}

TEST(NefStoreBounds, AFullStoreAnswersInsufficientResourcesEvenWhereQuotaExceededIsDefined) {
    auto d = spawn({{"NEF_STORE_MAX_ENTRIES", "1"}});
    ASSERT_GT(d.nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string collection =
        std::string(kNefBase) + "/3gpp-net-stat-report/v1/scs-sus02/subscriptions";
    ASSERT_TRUE(wait_reachable(client, collection, 200)) << "nef never reachable";

    const auto first = post(client, collection, net_status_subscription());
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->status, 201) << first->body;
    expect_problem(
        post(client, collection, net_status_subscription()), 500, "INSUFFICIENT_RESOURCES");
}
