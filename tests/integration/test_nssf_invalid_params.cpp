// SBI-03 invalidParams, step 2, NSSF (ADR-0629; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param for a query parameter is
// TS29571_CommonData.yaml's "query " plus the name. The NSSF's one NOTE 1 site it can name is
// NSSelectionGet without its required nf-type / nf-id (TS29531_Nnssf_NSSelection.yaml): each
// absent one is listed, a present one is not, and with both present the handler answers 200.
// Real nrf + nssf processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token.
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
constexpr const char* kNsSelection =
    "https://127.0.0.1:7801/nnssf-nsselection/v2/network-slice-information";
constexpr const char* kAmfInstanceId = "00000000-0000-4000-8000-000000000629";

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

class NssfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess nssf_{NSSF_PATH};
    sbi_core::http2::Client client_ = nf_test::oauth2_test_client();
    std::string token_;

    void SetUp() override {
        ASSERT_GT(nrf_.pid(), 0) << "failed to fork nrf";
        ASSERT_GT(nssf_.pid(), 0) << "failed to fork nssf";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7777)) << "nrf not listening";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7801)) << "nssf not listening";
        // Retried until the NRF answers with a token: it may refuse one before the NSSF's
        // NFRegister has gone through.
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                token_ = nf_test::oauth2_test_token(client_, "nnssf-nsselection", "NSSF");
                return !token_.empty();
            },
            std::chrono::seconds(15)))
            << "failed to obtain OAuth2 token from nrf";
    }

    sbi_core::http2::ClientResponse get(const std::string& query) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = std::string(kNsSelection) + query;
        req.headers.emplace("authorization", "Bearer " + token_);
        auto resp = client_.send(req);
        EXPECT_TRUE(resp.has_value()) << query;
        return resp.has_value() ? *resp : sbi_core::http2::ClientResponse{};
    }

    json expect_missing(const std::string& query) {
        const auto resp = get(query);
        EXPECT_EQ(resp.status, 400) << query << ": " << resp.body;
        const auto problem = json::parse(resp.body, nullptr, false);
        EXPECT_EQ(problem.value("cause", ""), "MANDATORY_QUERY_PARAM_MISSING") << query;
        return problem;
    }
};

TEST_F(NssfInvalidParams, NoQueryParametersNamesBothRequiredOnes) {
    const auto problem = expect_missing("");
    EXPECT_EQ(params_of(problem), (std::vector<std::string>{"query nf-type", "query nf-id"}))
        << problem.dump();
}

TEST_F(NssfInvalidParams, MissingNfIdNamesOnlyNfId) {
    const auto problem = expect_missing("?nf-type=AMF");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"query nf-id"}) << problem.dump();
}

TEST_F(NssfInvalidParams, MissingNfTypeNamesOnlyNfType) {
    const auto problem = expect_missing(std::string("?nf-id=") + kAmfInstanceId);
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"query nf-type"}) << problem.dump();
}

TEST_F(NssfInvalidParams, BothPresentReachesTheSelection) {
    const auto resp = get(std::string("?nf-type=AMF&nf-id=") + kAmfInstanceId);
    EXPECT_EQ(resp.status, 200) << resp.body;
}

} // namespace
