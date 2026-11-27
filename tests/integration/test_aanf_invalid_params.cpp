// SBI-03 invalidParams, step 2, AAnF (ADR-0639; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "\"invalidParams\" attribute shall be included in the \"ProblemDetails\"
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param for a body IE is a JSON Pointer
// (TS29571_CommonData.yaml). The AAnF's five NOTE 1 sites (TS29535_Naanf_AKMA.yaml):
// register-anchorkey with a bad kAkma / aKId, without supi or with gpsi, and with a supi off the
// Supi pattern; retrieve-applicationkey with a malformed afId; remove-context without supi. A valid
// registration answers 200, the positive control.
// Real nrf + aanf processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token; register and remove
// are sent with the AUSF's certificate (SEC-03, as test_aanf_authorization.cpp). Needs Valkey.
#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <vector>

#include "oauth2_enforcement.hpp"
#include "spawn_guard.hpp"

#include <gtest/gtest.h>

namespace {
using nlohmann::json;
constexpr const char* kAanf = "https://127.0.0.1:7824/naanf-akma/v1";
// This file's own A-KID and SUPI: test_aanf_authorization.cpp expects no context for
// x@akma.example, and Valkey outlives the processes, so the positive control also removes what it
// registers.
constexpr const char* kAkid = "invalid-params@akma.example";
constexpr const char* kSupi = "imsi-999700000016016";
constexpr const char* kKakma = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

sbi_core::http2::Client client_as(const std::string& name) {
    return sbi_core::http2::Client(sbi_core::http2::TlsConfig{
        .cert_path = std::string(CERTS_DIR) + "/" + name + "/cert.pem",
        .key_path = std::string(CERTS_DIR) + "/" + name + "/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    });
}

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

class AanfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess aanf_{AANF_PATH};
    sbi_core::http2::Client ausf_ = client_as("ausf");
    sbi_core::http2::Client af_ = client_as("af.example.com");
    std::string token_;

    void SetUp() override {
        ASSERT_GT(nrf_.pid(), 0) << "failed to fork nrf";
        ASSERT_GT(aanf_.pid(), 0) << "failed to fork aanf";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7777)) << "nrf not listening";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7824)) << "aanf not listening";
        // Retried until the NRF answers with a token: it may refuse one before the AAnF's
        // NFRegister has gone through.
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                token_ = nf_test::oauth2_test_token(af_, "naanf-akma", "AANF");
                return !token_.empty();
            },
            std::chrono::seconds(15)))
            << "failed to obtain OAuth2 token from nrf";
    }

    sbi_core::http2::ClientResponse
    post(sbi_core::http2::Client& c, const std::string& op, const json& body) {
        return nf_test::send_to(
            c, {.method = "POST", .url = std::string(kAanf) + op, .body = body.dump()}, token_);
    }

    static json expect_400(const sbi_core::http2::ClientResponse& resp, const std::string& cause) {
        EXPECT_EQ(resp.status, 400) << resp.body;
        const auto problem = json::parse(resp.body, nullptr, false);
        EXPECT_EQ(problem.value("cause", ""), cause) << resp.body;
        return problem;
    }

    static json key_info() { return json{{"supi", kSupi}, {"aKId", kAkid}, {"kAkma", kKakma}}; }
};

TEST_F(AanfInvalidParams, BadKakmaAndAkidAreBothNamed) {
    auto body = key_info();
    body["kAkma"] = "abc";
    body["aKId"] = "no-at-sign";
    const auto problem =
        expect_400(post(ausf_, "/register-anchorkey", body), "MANDATORY_IE_INCORRECT");
    EXPECT_EQ(params_of(problem), (std::vector<std::string>{"/kAkma", "/aKId"})) << problem.dump();
}

TEST_F(AanfInvalidParams, GpsiInsteadOfSupiNamesBoth) {
    auto body = key_info();
    body.erase("supi");
    body["gpsi"] = "msisdn-15550100001";
    const auto problem =
        expect_400(post(ausf_, "/register-anchorkey", body), "MANDATORY_IE_MISSING");
    EXPECT_EQ(params_of(problem), (std::vector<std::string>{"/supi", "/gpsi"})) << problem.dump();
}

TEST_F(AanfInvalidParams, SupiOffThePatternIsNamed) {
    auto body = key_info();
    // TS29571_CommonData.yaml Supi ends in "|.+", so only an empty value or one with a line
    // terminator is off the pattern.
    body["supi"] = "";
    const auto problem =
        expect_400(post(ausf_, "/register-anchorkey", body), "MANDATORY_IE_INCORRECT");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/supi"}) << problem.dump();
}

TEST_F(AanfInvalidParams, MalformedAfIdIsNamed) {
    const auto problem = expect_400(
        post(af_, "/retrieve-applicationkey", json{{"afId", "af.example.com"}, {"aKId", kAkid}}),
        "MANDATORY_IE_INCORRECT");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/afId"}) << problem.dump();
}

TEST_F(AanfInvalidParams, RemoveContextWithoutSupiNamesSupi) {
    const auto problem =
        expect_400(post(ausf_, "/remove-context", json::object()), "MANDATORY_IE_MISSING");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/supi"}) << problem.dump();
}

TEST_F(AanfInvalidParams, ValidRegistrationIsAccepted) {
    const auto resp = post(ausf_, "/register-anchorkey", key_info());
    EXPECT_EQ(resp.status, 200) << resp.body;
    const auto removed = post(ausf_, "/remove-context", json{{"supi", kSupi}});
    EXPECT_EQ(removed.status, 204) << removed.body;
}

} // namespace
