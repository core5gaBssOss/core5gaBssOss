// SBI-03 invalidParams, step 2, NRF (ADR-0613; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param encodings are TS29571_CommonData.yaml's:
// "query " plus the name for a query parameter, a JSON Pointer for a body attribute. One probe per
// kind of NRF site:
//   - NFDiscover without target-nf-type (a missing required query parameter);
//   - NFDiscover with a requester-plmn-list that is not a JSON array (an incorrect one);
//   - NFRegister of an NFProfile lacking two of its three required members (only those listed);
//   - NFStatusSubscribe without nfStatusNotificationUri;
//   - RetrieveKeyRequest without headerParameters (issuer present, so only one entry).
// Real nrf process over TLS 1.3 + mTLS HTTP/2 with a real NRF token where the route needs one.
#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "oauth2_enforcement.hpp"
#include "spawn_guard.hpp"

#include <gtest/gtest.h>

namespace {
using nlohmann::json;
constexpr const char* kNrf = "https://127.0.0.1:7777";

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

class NrfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    sbi_core::http2::Client client_ = nf_test::oauth2_test_client();

    void SetUp() override {
        ASSERT_GT(nrf_.pid(), 0) << "failed to fork nrf";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7777)) << "nrf is not listening on 7777";
    }

    std::string token(const std::string& scope) {
        auto t = nf_test::oauth2_test_token(client_, scope, "NRF");
        EXPECT_FALSE(t.empty()) << "failed to obtain an OAuth2 token for " << scope;
        return t;
    }

    // Sends the request and returns the 400 ProblemDetails, after checking status and cause.
    json expect_400(const std::string& method,
                    const std::string& path,
                    const std::string& cause,
                    const std::string& body,
                    const std::string& bearer) {
        const nf_test::EnforcedRoute route{
            .method = method, .url = std::string(kNrf) + path, .body = body};
        const auto resp = nf_test::send_to(client_, route, bearer);
        EXPECT_EQ(resp.status, 400) << path << ": " << resp.body;
        const auto problem = json::parse(resp.body, nullptr, false);
        if (!problem.is_object()) {
            ADD_FAILURE() << path << ": not a JSON body: " << resp.body;
            return json::object();
        }
        EXPECT_EQ(problem.value("cause", ""), cause) << path;
        return problem;
    }
};

TEST_F(NrfInvalidParams, DiscoveryWithoutTargetNfTypeNamesTheQueryParameter) {
    const auto problem = expect_400("GET",
                                    "/nnrf-disc/v1/nf-instances?requester-nf-type=AMF",
                                    "MANDATORY_QUERY_PARAM_MISSING",
                                    "",
                                    token("nnrf-disc"));
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"query target-nf-type"})
        << problem.dump();
}

TEST_F(NrfInvalidParams, NonArrayRequesterPlmnListNamesTheQueryParameter) {
    const auto problem = expect_400("GET",
                                    "/nnrf-disc/v1/nf-instances?target-nf-type=UDM&"
                                    "requester-nf-type=AMF&requester-plmn-list=not-json",
                                    "MANDATORY_IE_INCORRECT",
                                    "",
                                    token("nnrf-disc"));
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"query requester-plmn-list"})
        << problem.dump();
}

TEST_F(NrfInvalidParams, RegisterListsOnlyTheAbsentRequiredNfProfileMembers) {
    // nfType is present; nfInstanceId and nfStatus are not. NFRegister needs no token (ADR-0499).
    const auto problem =
        expect_400("PUT",
                   "/nnrf-nfm/v1/nf-instances/4c1f2a3b-0000-4000-8000-000000000613",
                   "MANDATORY_IE_MISSING",
                   R"({"nfType":"AMF","ipv4Addresses":["127.0.0.1"]})",
                   "");
    EXPECT_EQ(params_of(problem), (std::vector<std::string>{"/nfInstanceId", "/nfStatus"}))
        << problem.dump();
}

TEST_F(NrfInvalidParams, SubscribeWithoutNotificationUriNamesTheBodyAttribute) {
    const auto problem = expect_400("POST",
                                    "/nnrf-nfm/v1/subscriptions",
                                    "MANDATORY_IE_MISSING",
                                    R"({"reqNfType":"AMF"})",
                                    token("nnrf-nfm"));
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/nfStatusNotificationUri"})
        << problem.dump();
}

TEST_F(NrfInvalidParams, RetrieveKeyWithoutHeaderParametersNamesOnlyThatMember) {
    // TS29510_Nnrf_AccessToken.yaml declares no security scheme for this operation (ADR-0499).
    const auto problem = expect_400("POST",
                                    "/oauth2/retrieve-key",
                                    "MANDATORY_IE_MISSING",
                                    R"({"issuer":"5ba9a927-1d31-4c8e-8a10-000000000001"})",
                                    "");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/headerParameters"}) << problem.dump();
}

} // namespace
