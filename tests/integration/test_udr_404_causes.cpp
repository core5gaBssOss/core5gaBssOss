// SBI-03, UDR part (ADR-0583): every 404 the UDR's Nudr_DataRepository routes answer carries a
// TS 29.504 table 6.1.6-2 / TS 29.500 table 5.2.7.2-1 `cause`. One probe per class:
//   - DATA_NOT_FOUND (TS 29.504 table 6.1.6-2): the data named in the request is not in the UDR;
//   - SUBSCRIPTION_NOT_FOUND (TS 29.500 table 5.2.7.2-1): an individual subscription resource;
//   - GROUP_IDENTIFIER_NOT_FOUND (TS 29.504 table 6.1.6-2; TS 29.505 5.2.33/5.2.35/5.2.55): the
//     group identifier does not exist.
// Real nrf + udr processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token.
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
constexpr const char* kApi = "https://127.0.0.1:7781/nudr-dr/v2/";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

std::string fetch_token() {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body =
        "grant_type=client_credentials&nfInstanceId=" + std::string(nf_test::kTestAfInstanceId) +
        "&scope=nudr-dr&targetNfType=UDR";
    auto resp = nf_test::send_token_request(req);
    for (int attempt = 0; attempt < 50 && (!resp.has_value() || resp->status != 200); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // the UDR's NFRegister
        resp = nf_test::send_token_request(req);
    }
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

nf_test::SpawnedProcess spawn_udr_after_nrf() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(UDR_PATH);
}

class UdrNotFoundCauses : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess udr_ = spawn_udr_after_nrf();
    sbi_core::http2::Client client_ = make_client();
    std::string token_;

    void SetUp() override {
        nf_test::wait_tcp_listening(7777);
        nf_test::wait_tcp_listening(7781);
        token_ = fetch_token();
        ASSERT_FALSE(token_.empty()) << "failed to obtain OAuth2 token from nrf";
    }

    void expect_cause(const std::string& path, const std::string& cause) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = std::string(kApi) + path;
        req.headers.emplace("authorization", "Bearer " + token_);
        auto resp = client_.send(req);
        ASSERT_TRUE(resp.has_value()) << path;
        ASSERT_EQ(resp->status, 404) << path << ": " << resp->body;
        EXPECT_EQ(json::parse(resp->body).value("cause", ""), cause) << path;
    }
};

TEST_F(UdrNotFoundCauses, MissingDataIsDataNotFound) {
    expect_cause("subscription-data/imsi-999700000009983/context-data/amf-3gpp-access",
                 "DATA_NOT_FOUND");
}

TEST_F(UdrNotFoundCauses, MissingSubscriptionIsSubscriptionNotFound) {
    expect_cause("application-data/subs-to-notify/adr-0583-no-such-subscription",
                 "SUBSCRIPTION_NOT_FOUND");
}

TEST_F(UdrNotFoundCauses, MissingGroupIsGroupIdentifierNotFound) {
    expect_cause("subscription-data/group-data/5g-vn-groups/extgroup-adr-0583@example.com",
                 "GROUP_IDENTIFIER_NOT_FOUND");
}

} // namespace
