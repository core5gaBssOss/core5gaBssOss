// ADR-0601 (SBI-03, NEF part 2): 404s for an unknown NEF resource carry the cause the
// specification defines for it.
//   - Individual ... Subscription resources: SUBSCRIPTION_NOT_FOUND (TS 29.500 table 5.2.7.2-1),
//     on GET as well as DELETE (ADR-0589/ADR-0590 precedent), and on the monitoring
//     notification callback for an unknown subscription.
//   - Nnef_SMContext release of an unknown SM context: CONTEXT_NOT_FOUND (TS 29.541
//     table 6.1.7.3-1).
//   - Individual MBS Session: MBS_SESSION_CONTEXT_NOT_FOUND (TS 29.522 table 5.20.7.3-1).
// Drives nrf and nef as real processes over TLS 1.3 + mTLS HTTP/2 with real NRF tokens.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

struct Case {
    const char* method;
    std::string path;
    std::string body;
    const char* cause;
};

} // namespace

TEST(NefSbi03Causes, UnknownResourcesCarryTheirSpecCause) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    nf_test::SpawnedProcess nef(NEF_PATH);
    ASSERT_GT(nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    ASSERT_TRUE(
        wait_reachable(client, std::string(kNefBase) + "/3gpp-ue-address/v1/subscriptions/x", 200))
        << "nef never reachable";

    const std::vector<Case> cases = {
        // Subscriptions that had no cause on any method.
        {"GET", "/3gpp-5glan-pp/v1/af-sbi03/subscriptions/nope", "", "SUBSCRIPTION_NOT_FOUND"},
        {"DELETE", "/3gpp-5glan-pp/v1/af-sbi03/subscriptions/nope", "", "SUBSCRIPTION_NOT_FOUND"},
        {"DELETE", "/3gpp-acs-pp/v1/af-sbi03/subscriptions/nope", "", "SUBSCRIPTION_NOT_FOUND"},
        {"DELETE",
         "/3gpp-pdtq-policy-negotiation/v1/af-sbi03/subscriptions/nope",
         "",
         "SUBSCRIPTION_NOT_FOUND"},
        {"DELETE",
         "/3gpp-applying-bdt-policy/v1/af-sbi03/subscriptions/nope",
         "",
         "SUBSCRIPTION_NOT_FOUND"},
        {"GET", "/3gpp-ue-address/v1/subscriptions/nope", "", "SUBSCRIPTION_NOT_FOUND"},
        {"DELETE",
         "/3gpp-mbs-session/v1/mbs-sessions/subscriptions/nope",
         "",
         "SUBSCRIPTION_NOT_FOUND"},
        {"DELETE",
         "/3gpp-cp-parameter-provisioning/v1/af-sbi03/subscriptions/nope",
         "",
         "SUBSCRIPTION_NOT_FOUND"},
        {"DELETE", "/3gpp-time-sync/v1/af-sbi03/subscriptions/nope", "", "SUBSCRIPTION_NOT_FOUND"},
        // GET of a subscription whose PUT/PATCH/DELETE already carried the cause.
        {"GET",
         "/3gpp-traffic-influence/v1/af-sbi03/subscriptions/nope",
         "",
         "SUBSCRIPTION_NOT_FOUND"},
        {"GET", "/nnef-eventexposure/v1/subscriptions/nope", "", "SUBSCRIPTION_NOT_FOUND"},
        // Notification for an unknown subscription.
        {"POST",
         "/nnef-callback/v1/monitoring-notify/af-sbi03/nope",
         "{}",
         "SUBSCRIPTION_NOT_FOUND"},
        // Nnef_SMContext.
        {"POST",
         "/nnef-smcontext/v1/sm-contexts/nope/release",
         json{{"cause", "PDU_SESSION_RELEASED"}}.dump(),
         "CONTEXT_NOT_FOUND"},
        // Individual MBS Session.
        {"DELETE", "/3gpp-mbs-session/v1/mbs-sessions/nope", "", "MBS_SESSION_CONTEXT_NOT_FOUND"},
    };

    for (const auto& c : cases) {
        sbi_core::http2::ClientRequest req;
        req.method = c.method;
        req.url = std::string(kNefBase) + c.path;
        if (!c.body.empty()) {
            req.headers.emplace("content-type", "application/json");
            req.body = c.body;
        }
        auto resp = client.send(req);
        ASSERT_TRUE(resp.has_value()) << c.method << " " << c.path;
        EXPECT_EQ(resp->status, 404) << c.method << " " << c.path << " " << resp->body;
        EXPECT_EQ(json::parse(resp->body).value("cause", ""), c.cause)
            << c.method << " " << c.path << " " << resp->body;
    }
}
