// REVIEW-5ae0ef1 SBI-03 (ADR-0534): the BSF's two duplicate-binding rejections that TS 29.521
// defines carry their application error. 4.2.2.2: a second PCF binding for the same supi + dnn +
// snssai is 403 with ExtProblemDetails holding the existing PCF and cause
// EXISTING_BINDING_INFO_FOUND; 4.2.2.4 step 2: a second MBS session binding for the same
// mbsSessionId is 403 with MbsExtProblemDetails and the same cause (table 5.7.3-1). Real nrf + bsf
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

constexpr const char* kBsfBase = "https://127.0.0.1:7789/nbsf-management/v1";

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

sbi_core::http2::ClientRequest post(const std::string& url, const json& body) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = url;
    req.headers.emplace("content-type", "application/json");
    req.body = body.dump();
    return req;
}

} // namespace

TEST(BsfExistingBinding, ASecondPduSessionBindingIs403ExistingBindingInfoFound) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0);
    nf_test::SpawnedProcess bsf(BSF_PATH);
    ASSERT_GT(bsf.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string url = std::string(kBsfBase) + "/pcfBindings";
    ASSERT_TRUE(wait_reachable(client, url, 200)) << "bsf never reachable";

    const json binding{{"supi", "imsi-001010000000101"},
                       {"dnn", "internet"},
                       {"snssai", {{"sst", 1}, {"sd", "000001"}}},
                       {"ipv4Addr", "10.45.0.101"},
                       {"pcfSmFqdn", "pcf-a.example.com"}};
    auto first = client.send(post(url, binding));
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->status, 201) << first->body;

    json second_binding = binding;
    second_binding["pcfSmFqdn"] = "pcf-b.example.com";
    auto second = client.send(post(url, second_binding));
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->status, 403) << second->body;
    const auto problem = json::parse(second->body);
    EXPECT_EQ(problem.value("cause", ""), "EXISTING_BINDING_INFO_FOUND") << second->body;
    EXPECT_EQ(problem.value("pcfSmFqdn", ""), "pcf-a.example.com") << second->body;
}

TEST(BsfExistingBinding, ASecondMbsSessionBindingIs403ExistingBindingInfoFound) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0);
    nf_test::SpawnedProcess bsf(BSF_PATH);
    ASSERT_GT(bsf.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string url = std::string(kBsfBase) + "/pcf-mbs-bindings";
    ASSERT_TRUE(wait_reachable(client, url, 200)) << "bsf never reachable";

    const json binding{
        {"mbsSessionId",
         {{"tmgi", {{"mbsServiceId", "A1B2C3"}, {"plmnId", {{"mcc", "001"}, {"mnc", "01"}}}}}}},
        {"pcfFqdn", "pcf-mbs-a.example.com"}};
    auto first = client.send(post(url, binding));
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->status, 201) << first->body;

    json second_binding = binding;
    second_binding["pcfFqdn"] = "pcf-mbs-b.example.com";
    auto second = client.send(post(url, second_binding));
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->status, 403) << second->body;
    const auto problem = json::parse(second->body);
    EXPECT_EQ(problem.value("cause", ""), "EXISTING_BINDING_INFO_FOUND") << second->body;
    EXPECT_EQ(problem.value("pcfFqdn", ""), "pcf-mbs-a.example.com") << second->body;
}
