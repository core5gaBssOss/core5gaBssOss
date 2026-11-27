// SBI-03 invalidParams, step 2, NEF (ADR-0623; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param encodings are TS29571_CommonData.yaml's:
// "query " plus the name for a query parameter, "header " plus the name for a header, a JSON
// Pointer for a body attribute. One probe per kind of NEF site:
//   - a missing required query parameter (Nnef_PFDmanagement AllFetch without application-ids);
//   - a missing header (Nnef_SMService SendSMS without Content-Type);
//   - two body attributes of which one is required (TrafficInfluDataSub without dnns/snssais);
//   - a JSON Patch body that is not an array (IMS parameter provisioning, JSON Pointer "").
// No NEF site with a NOTE 1 cause names a path variable, so there is no probe for one.
// Real nrf + nef processes over TLS 1.3 + mTLS HTTP/2 with real NRF tokens.
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

constexpr const char* kNefBase = "https://127.0.0.1:7790";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

nf_test::SpawnedProcess spawn_nef_after_nrf() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(NEF_PATH);
}

class NefInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess nef_ = spawn_nef_after_nrf();
    sbi_core::http2::Client client_ = make_client();

    void SetUp() override {
        ASSERT_GT(nef_.pid(), 0);
        nf_test::attach_test_tokens(client_);
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                sbi_core::http2::ClientRequest req;
                req.method = "GET";
                req.url = std::string(kNefBase) + "/nnef-pfdmanagement/v1/applications";
                return client_.send(req).has_value();
            },
            std::chrono::seconds(20)))
            << "nef never reachable";
    }

    json send(const char* method,
              const std::string& path,
              const std::string& content_type,
              const std::string& body) {
        sbi_core::http2::ClientRequest req;
        req.method = method;
        req.url = std::string(kNefBase) + path;
        // An empty content_type sends "content-type:" with no value. libcurl then sends no
        // Content-Type at all; without that line it would add its own default
        // "application/x-www-form-urlencoded" for a POST (CURLOPT_POSTFIELDS in http2_client.cpp).
        req.headers.emplace("content-type", content_type);
        req.body = body;
        auto resp = client_.send(req);
        if (!resp.has_value()) {
            ADD_FAILURE() << method << " " << path << ": no response";
            return json::object();
        }
        EXPECT_EQ(resp->status, 400) << method << " " << path << " " << resp->body;
        return json::parse(resp->body, nullptr, false);
    }
};

} // namespace

TEST_F(NefInvalidParams, MissingApplicationIdsNamesTheQueryParameter) {
    const auto problem = send("GET", "/nnef-pfdmanagement/v1/applications", "", "");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_QUERY_PARAM_MISSING") << problem.dump();
    const json expected =
        json::array({{{"param", "query application-ids"}, {"reason", "missing"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(NefInvalidParams, SendSmsWithoutContentTypeNamesTheHeader) {
    const auto problem =
        send("POST", "/nnef-smservice/v1/sm-contexts/imsi-999700000000401/sendsms", "", "");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const json expected = json::array({{{"param", "header Content-Type"}, {"reason", "missing"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(NefInvalidParams, TrafficInfluenceDataWithoutDnnsOrSnssaisNamesBoth) {
    const json body{{"notifUri", "https://example.com/traffic-influence-notify"},
                    {"notifCorrId", "corr-invalid-params"}};
    const auto problem = send(
        "POST", "/nnef-traffic-influence-data/v1/subscriptions", "application/json", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const std::string reason = "absent; one of dnns and snssais is required";
    const json expected = json::array(
        {{{"param", "/dnns"}, {"reason", reason}}, {{"param", "/snssais"}, {"reason", reason}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(NefInvalidParams, ImsParameterProvisioningPatchNotAnArrayNamesTheWholeBody) {
    const auto problem =
        send("PATCH", "/3gpp-ims-pp/v1/pp/nope", "application/json-patch+json", "{}");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_INCORRECT") << problem.dump();
    const json expected = json::array({{{"param", ""}, {"reason", "not a non-empty array"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}
