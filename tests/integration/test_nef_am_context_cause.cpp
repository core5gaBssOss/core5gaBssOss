// ADR-0578 (SBI-03, NEF part): TS29522_AMPolicyAuthorization. A request for an Individual
// Application AM Context that does not exist is 404 with the application error
// APPLICATION_AM_CONTEXT_NOT_FOUND (TS 29.522 table 5.17.5.3-1). Drives nrf and nef as real
// processes over TLS 1.3 + mTLS HTTP/2 with real NRF tokens.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <utility>

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

} // namespace

TEST(NefAmContextCause, UnknownContextIs404WithApplicationAmContextNotFound) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    nf_test::SpawnedProcess nef(NEF_PATH);
    ASSERT_GT(nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string url =
        std::string(kNefBase) + "/3gpp-am-policyauthorization/v1/af-sbi03/app-am-contexts/nope";
    ASSERT_TRUE(wait_reachable(client, url, 200)) << "nef never reachable";

    for (const char* method : {"GET", "DELETE", "PATCH"}) {
        sbi_core::http2::ClientRequest req;
        req.method = method;
        req.url = url;
        if (std::string(method) == "PATCH") {
            req.headers.emplace("content-type", "application/merge-patch+json");
            req.body = "{}";
        }
        auto resp = client.send(req);
        ASSERT_TRUE(resp.has_value()) << method;
        EXPECT_EQ(resp->status, 404) << method << " " << resp->body;
        EXPECT_EQ(json::parse(resp->body).value("cause", ""), "APPLICATION_AM_CONTEXT_NOT_FOUND")
            << method << " " << resp->body;
    }
}
