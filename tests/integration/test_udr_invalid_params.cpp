// SBI-03 invalidParams, step 2, UDR (ADR-0609; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param encodings are TS29571_CommonData.yaml's:
// "query " plus the name for a query parameter, a JSON Pointer for a body attribute. One probe per
// kind of UDR site:
//   - one missing required query parameter (subs-to-notify GET without ue-id);
//   - several required query parameters, only the absent ones listed (nidd-authorization-data);
//   - an incorrect query parameter value (single-nssai that is not JSON);
//   - an incorrect body attribute (a subscription expiry that is not an RFC 3339 date-time);
//   - a JSON Patch body that is not an array (the whole body, JSON Pointer "").
// Real nrf + udr processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token.
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

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

class UdrInvalidParams : public ::testing::Test {
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

    // Sends the request and returns the 400 ProblemDetails, after checking status and cause.
    json expect_400(const std::string& method,
                    const std::string& path,
                    const std::string& cause,
                    const std::string& body = "",
                    const std::string& content_type = "application/json") {
        sbi_core::http2::ClientRequest req;
        req.method = method;
        req.url = std::string(kApi) + path;
        req.headers.emplace("authorization", "Bearer " + token_);
        if (!body.empty()) {
            req.headers.emplace("content-type", content_type);
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

TEST_F(UdrInvalidParams, MissingUeIdNamesTheQueryParameter) {
    const auto problem =
        expect_400("GET", "subscription-data/subs-to-notify", "MANDATORY_QUERY_PARAM_MISSING");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"query ue-id"}) << problem.dump();
}

TEST_F(UdrInvalidParams, OnlyTheAbsentRequiredQueryParametersAreListed) {
    // dnn is present; single-nssai and mtc-provider-information are not.
    const auto problem =
        expect_400("GET",
                   "subscription-data/imsi-999700000009984/nidd-authorization-data?dnn=internet",
                   "MANDATORY_QUERY_PARAM_MISSING");
    EXPECT_EQ(params_of(problem),
              (std::vector<std::string>{"query single-nssai", "query mtc-provider-information"}))
        << problem.dump();
}

TEST_F(UdrInvalidParams, MalformedSingleNssaiNamesTheQueryParameter) {
    const auto problem = expect_400("GET",
                                    "subscription-data/imsi-999700000009984/"
                                    "nidd-authorization-data?single-nssai=not-json&dnn=internet&"
                                    "mtc-provider-information=adr-0609",
                                    "MANDATORY_QUERY_PARAM_INCORRECT");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"query single-nssai"}) << problem.dump();
}

TEST_F(UdrInvalidParams, BadExpiryNamesTheBodyAttributeAsAJsonPointer) {
    // TS29505 SubscriptionDataSubscriptions: monitoredResourceUris and callbackReference required.
    const json body{{"callbackReference", "https://example.com/udr-cb"},
                    {"monitoredResourceUris",
                     json::array({"https://127.0.0.1:7781/nudr-dr/v2/subscription-data/"
                                  "imsi-999700000009984/context-data/amf-3gpp-access"})},
                    {"expiry", "next tuesday"}};
    const auto problem = expect_400(
        "POST", "subscription-data/subs-to-notify", "OPTIONAL_IE_INCORRECT", body.dump());
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/expiry"}) << problem.dump();
}

TEST_F(UdrInvalidParams, NonArrayJsonPatchNamesTheWholeBody) {
    const auto problem = expect_400("PATCH",
                                    "aiot-data/aiot-device-profile-data?supported-features=0",
                                    "MANDATORY_IE_INCORRECT",
                                    R"({"op":"replace"})",
                                    "application/json-patch+json");
    // RFC 6901: the empty JSON Pointer "" identifies the whole document.
    EXPECT_EQ(params_of(problem), std::vector<std::string>{""}) << problem.dump();
}

} // namespace
