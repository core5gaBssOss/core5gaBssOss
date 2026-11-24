// SBI-03 invalidParams, step 2, SMSF (ADR-0632; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param encodings are TS29571_CommonData.yaml's;
// a header is "header " plus its name.
//
// The SMSF's two NOTE 1 sites that name a parameter are the missing Content-Type of SendSMS and
// SendMtSMS (TS29540_Nsmsf_SMService.yaml, both multipart/related). Both handlers look the UE SMS
// context up first (404 CONTEXT_NOT_FOUND), so each probe activates one with
// SMServiceActivation (PUT /ue-contexts/{supi}) before it.
// Real nrf + smsf processes over TLS 1.3 + mTLS HTTP/2 with real NRF tokens.
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

constexpr const char* kSmsfApi = "https://127.0.0.1:7793/nsmsf-sms/v2";
constexpr const char* kSupi = "imsi-999700000000632";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

nf_test::SpawnedProcess spawn_smsf_after_nrf() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(SMSF_PATH);
}

class SmsfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess smsf_ = spawn_smsf_after_nrf();
    sbi_core::http2::Client client_ = make_client();

    void SetUp() override {
        ASSERT_GT(nrf_.pid(), 0);
        ASSERT_GT(smsf_.pid(), 0);
        nf_test::attach_test_tokens(client_);
        // Ready once the SMSF answers SMSServiceParameterUpdate for a SUPI it has no context for
        // (404): it is listening and accepts the NRF token.
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                sbi_core::http2::ClientRequest req;
                req.method = "PATCH";
                req.url = std::string(kSmsfApi) + "/ue-contexts/" + kSupi;
                req.headers.emplace("content-type", "application/json-patch+json");
                req.body = "[]";
                const auto resp = client_.send(req);
                return resp.has_value() && resp->status == 404;
            },
            std::chrono::seconds(20)))
            << "smsf never reachable";

        sbi_core::http2::ClientRequest put;
        put.method = "PUT";
        put.url = std::string(kSmsfApi) + "/ue-contexts/" + kSupi;
        put.headers.emplace("content-type", "application/json");
        put.body = json{{"supi", kSupi},
                        {"amfId", "00000000-0000-4000-8000-000000000632"},
                        {"accessType", "3GPP_ACCESS"}}
                       .dump();
        const auto resp = client_.send(put);
        ASSERT_TRUE(resp.has_value()) << "SMServiceActivation: no response";
        ASSERT_EQ(resp->status, 201) << resp->body;
    }

    // POSTs to {apiRoot}/ue-contexts/{supi}/<operation> with no Content-Type and returns the
    // ProblemDetails of the 400 it must get.
    json post_without_content_type(const std::string& operation) {
        sbi_core::http2::ClientRequest req;
        req.method = "POST";
        req.url = std::string(kSmsfApi) + "/ue-contexts/" + kSupi + "/" + operation;
        // "content-type:" with no value makes libcurl send no Content-Type at all; without it
        // libcurl adds its own "application/x-www-form-urlencoded" for a POST (CURLOPT_POSTFIELDS
        // in http2_client.cpp), and the SMSF would answer the multipart parse failure instead.
        req.headers.emplace("content-type", "");
        auto resp = client_.send(req);
        if (!resp.has_value()) {
            ADD_FAILURE() << "POST " << operation << ": no response";
            return json::object();
        }
        EXPECT_EQ(resp->status, 400) << operation << " " << resp->body;
        return json::parse(resp->body, nullptr, false);
    }
};

json content_type_missing() {
    return json::array({{{"param", "header Content-Type"}, {"reason", "missing"}}});
}

} // namespace

TEST_F(SmsfInvalidParams, SendSmsWithoutContentTypeNamesTheHeader) {
    const auto problem = post_without_content_type("sendsms");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    EXPECT_EQ(problem.value("invalidParams", json()), content_type_missing()) << problem.dump();
}

TEST_F(SmsfInvalidParams, SendMtSmsWithoutContentTypeNamesTheHeader) {
    const auto problem = post_without_content_type("send-mt-sms");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    EXPECT_EQ(problem.value("invalidParams", json()), content_type_missing()) << problem.dump();
}
