// SBI-03 invalidParams, step 2, LMF (ADR-0636; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "\"invalidParams\" attribute shall be included in the \"ProblemDetails\"
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param for a body IE is a JSON Pointer
// (TS29571_CommonData.yaml). The LMF's two NOTE 1 sites (TS29572_Nlmf_Location.yaml):
// DetermineLocation with a malformed multipart/related body (MANDATORY_IE_INCORRECT, the whole
// body: "") and ConfigureUp with neither member of UpConfig's anyOf, supi / gpsi
// (MANDATORY_IE_MISSING, "/supi" and "/gpsi"). With supi present ConfigureUp answers 204.
// Real nrf + lmf processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token.
#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <vector>

#include "oauth2_enforcement.hpp"
#include "spawn_guard.hpp"

#include <gtest/gtest.h>

namespace {
using nlohmann::json;
constexpr const char* kApiRoot = "https://127.0.0.1:7795/nlmf-loc/v1";

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

class LmfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess lmf_{LMF_PATH};
    sbi_core::http2::Client client_ = nf_test::oauth2_test_client();
    std::string token_;

    void SetUp() override {
        ASSERT_GT(nrf_.pid(), 0) << "failed to fork nrf";
        ASSERT_GT(lmf_.pid(), 0) << "failed to fork lmf";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7777)) << "nrf not listening";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7795)) << "lmf not listening";
        // Retried until the NRF answers with a token: it may refuse one before the LMF's
        // NFRegister has gone through.
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                token_ = nf_test::oauth2_test_token(client_, "nlmf-loc", "LMF");
                return !token_.empty();
            },
            std::chrono::seconds(15)))
            << "failed to obtain OAuth2 token from nrf";
    }

    sbi_core::http2::ClientResponse
    post(const std::string& path, const std::string& content_type, const std::string& body) {
        sbi_core::http2::ClientRequest req;
        req.method = "POST";
        req.url = std::string(kApiRoot) + path;
        req.headers.emplace("authorization", "Bearer " + token_);
        req.headers.emplace("content-type", content_type);
        req.body = body;
        auto resp = client_.send(req);
        EXPECT_TRUE(resp.has_value()) << path;
        return resp.has_value() ? *resp : sbi_core::http2::ClientResponse{};
    }

    // UpConfig with both required members (TS29572_Nlmf_Location.yaml) and no UE identity.
    static json up_config_without_ue_id() {
        return json{{"upNotifyCallBackUri", "https://consumer.example/up-notify"},
                    {"notifCorrelationId", "corr-1"}};
    }
};

TEST_F(LmfInvalidParams, MalformedMultipartDetermineLocationNamesTheWholeBody) {
    // A multipart/related body with no boundary delimiter in it: no part can be read.
    const auto resp =
        post("/determine-location",
             "multipart/related; boundary=lmf-test-boundary; type=\"application/json\"",
             "not a multipart body");
    ASSERT_EQ(resp.status, 400) << resp.body;
    const auto problem = json::parse(resp.body, nullptr, false);
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_INCORRECT") << resp.body;
    EXPECT_EQ(params_of(problem), std::vector<std::string>{""}) << resp.body;
}

TEST_F(LmfInvalidParams, ConfigureUpWithNeitherSupiNorGpsiNamesBoth) {
    const auto resp = post("/configure-up", "application/json", up_config_without_ue_id().dump());
    ASSERT_EQ(resp.status, 400) << resp.body;
    const auto problem = json::parse(resp.body, nullptr, false);
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << resp.body;
    EXPECT_EQ(params_of(problem), (std::vector<std::string>{"/supi", "/gpsi"})) << resp.body;
}

TEST_F(LmfInvalidParams, ConfigureUpWithSupiIsStored) {
    auto body = up_config_without_ue_id();
    body["supi"] = "imsi-001010000000001";
    const auto resp = post("/configure-up", "application/json", body.dump());
    EXPECT_EQ(resp.status, 204) << resp.body;
}

} // namespace
