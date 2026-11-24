// SBI-03 invalidParams, step 2, UDM (ADR-0614; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param encodings are TS29571_CommonData.yaml's:
// "query " plus the name for a query parameter, a JSON Pointer for a body attribute, the variable
// name with its braces for a path variable. One probe per kind of UDM site:
//   - one missing required query parameter (GetRgAuthData without authenticated-ind);
//   - a missing query parameter the UDM answers with MANDATORY_IE_MISSING (GetSharedData without
//     shared-data-ids; the cause is the UDM's as before ADR-0614, see its Disclosed list);
//   - one-of-two query parameters, both absent (group-identifiers: ext-group-id, int-group-id);
//   - a path variable that is not usable ({supiOrSuci}: a SUCI that cannot be de-concealed);
//   - a body attribute that is missing for this case (servingNetworkId for eap-aka-prime);
//   - a body attribute that is not usable (/suci of Nudm_UEID Deconceal);
//   - an incorrect optional body attribute (an SDM subscription expires that is not a date-time).
// Real nrf + udm + udr processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token.
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
constexpr const char* kUdm = "https://127.0.0.1:7780/";
constexpr const char* kSupi = "imsi-999700000000001"; // seeded 5G_AKA subscriber, udm main.cpp

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

std::string fetch_token(const std::string& scope) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body =
        "grant_type=client_credentials&nfInstanceId=" + std::string(nf_test::kTestAfInstanceId) +
        "&scope=" + scope + "&targetNfType=UDM";
    auto resp = nf_test::send_token_request(req);
    for (int attempt = 0; attempt < 50 && (!resp.has_value() || resp->status != 200); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // the UDM's NFRegister
        resp = nf_test::send_token_request(req);
    }
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

nf_test::SpawnedProcess spawn_after_nrf(const char* path) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(path);
}

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

class UdmInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess udm_ = spawn_after_nrf(UDM_PATH);
    // ADR-0383: the UDM reads authentication data from the UDR over Nudr.
    nf_test::SpawnedProcess udr_{UDR_PATH};
    sbi_core::http2::Client client_ = make_client();

    void SetUp() override {
        nf_test::wait_tcp_listening(7777);
        nf_test::wait_tcp_listening(7780);
        nf_test::wait_tcp_listening(7781);
    }

    // Sends the request with a token for `scope` and returns the 400 ProblemDetails, after
    // checking status and cause.
    json expect_400(const std::string& scope,
                    const std::string& method,
                    const std::string& path,
                    const std::string& cause,
                    const std::string& body = "") {
        const auto token = fetch_token(scope);
        EXPECT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf for " << scope;
        sbi_core::http2::ClientRequest req;
        req.method = method;
        req.url = std::string(kUdm) + path;
        req.headers.emplace("authorization", "Bearer " + token);
        if (!body.empty()) {
            req.headers.emplace("content-type", "application/json");
            req.body = body;
        }
        auto resp = client_.send(req);
        EXPECT_TRUE(resp.has_value()) << path;
        if (!resp.has_value()) {
            return json::object();
        }
        EXPECT_EQ(resp->status, 400) << path << ": " << resp->body;
        const auto problem = json::parse(resp->body);
        EXPECT_EQ(problem.value("cause", ""), cause) << path;
        return problem;
    }
};

TEST_F(UdmInvalidParams, MissingAuthenticatedIndNamesTheQueryParameter) {
    const auto problem =
        expect_400("nudm-ueau",
                   "GET",
                   "nudm-ueau/v1/" + std::string(kSupi) + "/security-information-rg",
                   "MANDATORY_QUERY_PARAM_MISSING");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"query authenticated-ind"})
        << problem.dump();
}

TEST_F(UdmInvalidParams, MissingSharedDataIdsNamesTheQueryParameter) {
    const auto problem =
        expect_400("nudm-sdm", "GET", "nudm-sdm/v2/shared-data", "MANDATORY_IE_MISSING");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"query shared-data-ids"})
        << problem.dump();
}

TEST_F(UdmInvalidParams, GroupIdentifiersWithNeitherIdNamesBoth) {
    const auto problem = expect_400(
        "nudm-sdm", "GET", "nudm-sdm/v2/group-data/group-identifiers", "MANDATORY_IE_MISSING");
    EXPECT_EQ(params_of(problem),
              (std::vector<std::string>{"query ext-group-id", "query int-group-id"}))
        << problem.dump();
}

TEST_F(UdmInvalidParams, SuciThatCannotBeDeconcealedNamesThePathVariable) {
    // Not the 8 dash-separated fields of a SUCI: deconceal_suci_if_needed refuses it.
    const auto problem =
        expect_400("nudm-ueau",
                   "GET",
                   "nudm-ueau/v1/suci-0-999-70/security-information-rg?authenticated-ind=true",
                   "MANDATORY_IE_INCORRECT");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"{supiOrSuci}"}) << problem.dump();
}

TEST_F(UdmInvalidParams, EapAkaPrimeWithoutServingNetworkIdNamesTheBodyAttribute) {
    // TS29503_Nudm_UEAU.yaml HssAuthenticationInfoRequest: hssAuthType and numOfRequestedVectors
    // are required; servingNetworkId is optional in the schema but needed for CK'/IK'.
    const json body{{"hssAuthType", "EAP_AKA_PRIME"}, {"numOfRequestedVectors", 1}};
    const auto problem = expect_400("nudm-ueau",
                                    "POST",
                                    "nudm-ueau/v1/" + std::string(kSupi) +
                                        "/hss-security-information/eap-aka-prime/generate-av",
                                    "MANDATORY_IE_MISSING",
                                    body.dump());
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/servingNetworkId"}) << problem.dump();
}

TEST_F(UdmInvalidParams, DeconcealOfAnUnusableSuciNamesTheBodyAttribute) {
    const json body{{"suci", "suci-0-999-70"}};
    const auto problem = expect_400(
        "nudm-ueid", "POST", "nudm-ueid/v1/deconceal", "MANDATORY_IE_INCORRECT", body.dump());
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/suci"}) << problem.dump();
}

TEST_F(UdmInvalidParams, BadSdmSubscriptionExpiresNamesTheBodyAttribute) {
    // TS29503_Nudm_SDM.yaml SdmSubscription: nfInstanceId, callbackReference and
    // monitoredResourceUris are required.
    const json body{{"nfInstanceId", "00000000-0000-4000-8000-000000000ccc"},
                    {"callbackReference", "https://example.com/cb"},
                    {"monitoredResourceUris", json::array({"https://example.com/res"})},
                    {"expires", "next tuesday"}};
    const auto problem = expect_400("nudm-sdm",
                                    "POST",
                                    "nudm-sdm/v2/" + std::string(kSupi) + "/sdm-subscriptions",
                                    "OPTIONAL_IE_INCORRECT",
                                    body.dump());
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/expires"}) << problem.dump();
}

} // namespace
