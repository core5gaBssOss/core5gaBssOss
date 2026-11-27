// SBI-03 invalidParams, step 2, MFAF (ADR-0637; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "\"invalidParams\" attribute shall be included in the \"ProblemDetails\"
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param for a body IE is a JSON Pointer
// (TS29571_CommonData.yaml). The MFAF's four MANDATORY_IE_MISSING sites:
// - Nmfaf_3daDataManagement Configure, MfafConfiguration with neither member of its oneOf:
//   "/messageConfigurations" and "/mfafTransferInfo";
// - the same, a MessageConfiguration with an empty notificationURI / correId:
//   "/messageConfigurations/{i}/notificationURI" / ".../correId";
// - Nmfaf_ContextManagement Transfer with an empty refIds: "/refIds";
// - the fetch callback (Nmfaf_3caDataManagement) with an empty id array: "" (the whole body).
// A valid configuration answers 201, the positive control.
// Real nrf + mfaf processes over TLS 1.3 + mTLS HTTP/2 with real NRF tokens. The MFAF needs
// Valkey; it starts without a Kafka broker. Skipped under ThreadSanitizer, as test_mfaf.cpp is.
#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "oauth2_enforcement.hpp"
#include "spawn_guard.hpp"

#include <gtest/gtest.h>

#if defined(__SANITIZE_THREAD__)
#define MFAF_INVALID_PARAMS_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define MFAF_INVALID_PARAMS_TSAN 1
#endif
#endif

namespace {
using nlohmann::json;
constexpr const char* kBase = "https://127.0.0.1:7800";

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

class MfafInvalidParams : public ::testing::Test {
protected:
    std::optional<nf_test::SpawnedProcess> nrf_;
    std::optional<nf_test::SpawnedProcess> mfaf_;
    sbi_core::http2::Client client_ = nf_test::oauth2_test_client();

    void SetUp() override {
#ifdef MFAF_INVALID_PARAMS_TSAN
        GTEST_SKIP()
            << "librdkafka's C11-thread broker threads are invisible to ThreadSanitizer -- "
               "MFAF test skipped, not passed (as test_mfaf.cpp)";
#endif
        nrf_.emplace(NRF_PATH);
        mfaf_.emplace(MFAF_PATH);
        ASSERT_GT(nrf_->pid(), 0) << "failed to fork nrf";
        ASSERT_GT(mfaf_->pid(), 0) << "failed to fork mfaf";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7777)) << "nrf not listening";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7800)) << "mfaf not listening";
    }

    // Retried until the NRF answers with a token: it may refuse one before the MFAF's
    // NFRegister has gone through.
    std::string token_for(const std::string& scope) {
        std::string token;
        EXPECT_TRUE(nf_test::wait_until_ready(
            [&] {
                token = nf_test::oauth2_test_token(client_, scope, "MFAF");
                return !token.empty();
            },
            std::chrono::seconds(15)))
            << "failed to obtain an OAuth2 token for " << scope;
        return token;
    }

    sbi_core::http2::ClientResponse
    post(const std::string& path, const json& body, const std::string& token) {
        sbi_core::http2::ClientRequest req;
        req.method = "POST";
        req.url = std::string(kBase) + path;
        if (!token.empty()) {
            req.headers.emplace("authorization", "Bearer " + token);
        }
        req.headers.emplace("content-type", "application/json");
        req.body = body.dump();
        auto resp = client_.send(req);
        EXPECT_TRUE(resp.has_value()) << path;
        return resp.has_value() ? *resp : sbi_core::http2::ClientResponse{};
    }

    static json expect_missing_ie(const sbi_core::http2::ClientResponse& resp) {
        EXPECT_EQ(resp.status, 400) << resp.body;
        const auto problem = json::parse(resp.body, nullptr, false);
        EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << resp.body;
        return problem;
    }
};

constexpr const char* kConfigurations = "/nmfaf-3dadatamanagement/v1/configurations";

TEST_F(MfafInvalidParams, ConfigurationWithNeitherOneOfMemberNamesBoth) {
    const auto problem = expect_missing_ie(
        post(kConfigurations, json::object(), token_for("nmfaf-3dadatamanagement")));
    EXPECT_EQ(params_of(problem),
              (std::vector<std::string>{"/messageConfigurations", "/mfafTransferInfo"}))
        << problem.dump();
}

TEST_F(MfafInvalidParams, EmptyRequiredMessageConfigurationMembersAreNamed) {
    const json cfg{
        {"messageConfigurations",
         json::array({{{"notificationURI", "https://consumer.example/n"}, {"correId", "c1"}},
                      {{"notificationURI", ""}, {"correId", ""}}})}};
    const auto problem =
        expect_missing_ie(post(kConfigurations, cfg, token_for("nmfaf-3dadatamanagement")));
    EXPECT_EQ(params_of(problem),
              (std::vector<std::string>{"/messageConfigurations/1/notificationURI",
                                        "/messageConfigurations/1/correId"}))
        << problem.dump();
}

TEST_F(MfafInvalidParams, ValidConfigurationIsCreated) {
    const json cfg{
        {"messageConfigurations",
         json::array({{{"notificationURI", "https://consumer.example/n"}, {"correId", "c1"}}})}};
    const auto resp = post(kConfigurations, cfg, token_for("nmfaf-3dadatamanagement"));
    EXPECT_EQ(resp.status, 201) << resp.body;
}

TEST_F(MfafInvalidParams, TransferWithEmptyRefIdsNamesRefIds) {
    const auto problem = expect_missing_ie(post("/nmfaf-contextmanagement/v1/transfer",
                                                json{{"refIds", json::array()}},
                                                token_for("nmfaf-contextmanagement")));
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/refIds"}) << problem.dump();
}

TEST_F(MfafInvalidParams, FetchWithNoIdsNamesTheWholeBody) {
    // The fetch callback takes no token (test_dccf_mfaf_authorization.cpp).
    const auto problem = expect_missing_ie(post("/mfaf-inbound/v1/fetch", json::array(), ""));
    EXPECT_EQ(params_of(problem), std::vector<std::string>{""}) << problem.dump();
}

} // namespace
