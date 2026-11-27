// SBI-03 invalidParams, step 2, PCF (ADR-0626; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param for a body attribute is a JSON Pointer
// (TS29571_CommonData.yaml).
//
// One probe per PCF NOTE 1 site, against real processes over TLS 1.3 + mTLS HTTP/2 with real NRF
// tokens: an NRF and a PCF (config/pcf.json, port 7783). The PCF keeps these resources in memory;
// no database, Valkey, UDR, CHF, BSF, AMF or UDM is needed for these error paths (the PCF only
// registers with the NRF at startup, from a background thread).
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

constexpr const char* kPcfBase = "https://127.0.0.1:7783";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

nf_test::SpawnedProcess spawn_pcf_after_nrf() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(PCF_PATH);
}

class PcfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess pcf_ = spawn_pcf_after_nrf();
    sbi_core::http2::Client client_ = make_client();

    void SetUp() override {
        ASSERT_GT(nrf_.pid(), 0);
        ASSERT_GT(pcf_.pid(), 0);
        nf_test::attach_test_tokens(client_);
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                sbi_core::http2::ClientRequest req;
                req.method = "GET";
                req.url = std::string(kPcfBase) +
                          "/npcf-am-policyauthorization/v1/app-am-contexts/nonexistent";
                const auto r = client_.send(req);
                return r.has_value() && r->status == 404; // routed and authorized
            },
            std::chrono::seconds(30)))
            << "pcf never reachable";
    }

    tl::expected<sbi_core::http2::ClientResponse, std::string>
    send(const char* method, const std::string& path, const char* content_type, const json& body) {
        sbi_core::http2::ClientRequest req;
        req.method = method;
        req.url = std::string(kPcfBase) + path;
        req.headers.emplace("content-type", content_type);
        req.body = body.dump();
        return client_.send(req);
    }

    // Sends the request and returns the ProblemDetails of the 400 it expects.
    json send_expect_400(const char* method,
                         const std::string& path,
                         const char* content_type,
                         const json& body) {
        auto resp = send(method, path, content_type, body);
        if (!resp.has_value()) {
            ADD_FAILURE() << method << " " << path << ": no response";
            return json::object();
        }
        EXPECT_EQ(resp->status, 400) << method << " " << path << " " << resp->body;
        return json::parse(resp->body, nullptr, false);
    }
};

} // namespace

// Npcf_PolicyAuthorization_Create: the handler requires AppSessionContext.ascReqData
// (TS29514_Npcf_PolicyAuthorization.yaml; optional in the schema, which also carries the response
// and notification members).
TEST_F(PcfInvalidParams, AppSessionWithoutAscReqDataNamesAscReqData) {
    const auto problem = send_expect_400(
        "POST", "/npcf-policyauthorization/v1/app-sessions", "application/json", json::object());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const json expected = json::array({{{"param", "/ascReqData"}, {"reason", "missing"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

// Npcf_AMPolicyAuthorization_Update: a context with no events subscription, patched with an
// evSubsc (AmEventsSubscDataRm, all members optional) that lacks eventNotifUri -- the merge does
// not yield an AmEventsSubscData, whose one required member is eventNotifUri
// (TS29534_Npcf_AMPolicyAuthorization.yaml).
TEST_F(PcfInvalidParams, AmContextPatchWithoutEventNotifUriNamesIt) {
    const json create_body{{"supi", "imsi-999700000000626"},
                           {"termNotifUri", "https://127.0.0.1:19996/am-auth-term"},
                           {"highThruInd", true}};
    auto created = send(
        "POST", "/npcf-am-policyauthorization/v1/app-am-contexts", "application/json", create_body);
    ASSERT_TRUE(created.has_value());
    ASSERT_EQ(created->status, 201) << created->body;
    const auto loc = created->headers.find("location");
    ASSERT_NE(loc, created->headers.end());
    const std::string id = loc->second.substr(loc->second.rfind('/') + 1);

    const json patch_body{{"evSubsc", {{"events", json::array({{{"event", "SAC_CH"}}})}}}};
    const auto problem = send_expect_400("PATCH",
                                         "/npcf-am-policyauthorization/v1/app-am-contexts/" + id,
                                         "application/merge-patch+json",
                                         patch_body);
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const json expected =
        json::array({{{"param", "/evSubsc/eventNotifUri"}, {"reason", "missing"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}
