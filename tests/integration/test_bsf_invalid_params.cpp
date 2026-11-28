// SBI-03 invalidParams, step 2, BSF (ADR-0628; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "invalidParams" attribute shall be included in the "ProblemDetails"
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s). InvalidParam.param encodings are TS29571_CommonData.yaml's: a
// JSON Pointer for a body attribute, "query " plus the name for a query parameter.
//
// One probe per BSF NOTE 1 site (nfs/bsf/src/main.cpp), plus the second branch of the
// mbs-session-id site:
//   - CreatePCFBinding without supi (MANDATORY_IE_MISSING, "/supi");
//   - GetPCFMbsBinding without mbs-session-id (MANDATORY_QUERY_PARAM_MISSING,
//     "query mbs-session-id", reason "missing");
//   - GetPCFMbsBinding with an mbs-session-id that is not JSON (same cause and param, reason
//     "not JSON").
//
// Real nrf + bsf processes over TLS 1.3 + mTLS HTTP/2 with real NRF tokens. The BSF keeps its
// bindings and subscriptions in memory: it needs config/bsf.json (port 7789), its certificates
// (certs/bsf, certs/ca, certs/nrf-jwt) and an NRF to issue tokens; no database, Valkey, Doris or
// peer NF.
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

constexpr const char* kBsfBase = "https://127.0.0.1:7789/nbsf-management/v1";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

nf_test::SpawnedProcess spawn_bsf_after_nrf() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(BSF_PATH);
}

class BsfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess bsf_ = spawn_bsf_after_nrf();
    sbi_core::http2::Client client_ = make_client();

    void SetUp() override {
        ASSERT_GT(nrf_.pid(), 0);
        ASSERT_GT(bsf_.pid(), 0);
        nf_test::attach_test_tokens(client_);
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                sbi_core::http2::ClientRequest req;
                req.method = "GET";
                req.url = std::string(kBsfBase) + "/pcf-ue-bindings";
                const auto r = client_.send(req);
                return r.has_value() && r->status == 200; // routed and authorized
            },
            std::chrono::seconds(30)))
            << "bsf never reachable";
    }

    // Sends the request, checks the 400 and its cause, and returns the ProblemDetails.
    json expect_400(const char* method,
                    const std::string& path,
                    const std::string& cause,
                    const std::string& body = "") {
        sbi_core::http2::ClientRequest req;
        req.method = method;
        req.url = std::string(kBsfBase) + path;
        if (!body.empty()) {
            req.headers.emplace("content-type", "application/json");
            req.body = body;
        }
        auto resp = client_.send(req);
        if (!resp.has_value()) {
            ADD_FAILURE() << method << " " << path << ": no response";
            return json::object();
        }
        EXPECT_EQ(resp->status, 400) << method << " " << path << " " << resp->body;
        auto problem = json::parse(resp->body, nullptr, false);
        EXPECT_EQ(problem.value("cause", ""), cause) << problem.dump();
        return problem;
    }
};

} // namespace

// TS29521_Nbsf_Management.yaml PcfBinding requires only dnn and snssai, so the body parses; the
// BSF itself requires supi for its duplicate-combination check.
TEST_F(BsfInvalidParams, PcfBindingWithoutSupiNamesSupi) {
    const json body{{"dnn", "internet"},
                    {"snssai", {{"sst", 1}, {"sd", "000001"}}},
                    {"ipv4Addr", "10.45.0.128"},
                    {"pcfSmFqdn", "pcf-adr0628.example.com"}};
    const auto problem = expect_400("POST", "/pcfBindings", "MANDATORY_IE_MISSING", body.dump());
    const json expected = json::array({{{"param", "/supi"}, {"reason", "missing"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(BsfInvalidParams, MbsBindingQueryWithoutMbsSessionIdNamesIt) {
    const auto problem = expect_400("GET", "/pcf-mbs-bindings", "MANDATORY_QUERY_PARAM_MISSING");
    const json expected = json::array({{{"param", "query mbs-session-id"}, {"reason", "missing"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(BsfInvalidParams, MbsBindingQueryWithNonJsonMbsSessionIdNamesIt) {
    const auto problem = expect_400(
        "GET", "/pcf-mbs-bindings?mbs-session-id=not-json", "MANDATORY_QUERY_PARAM_MISSING");
    const json expected =
        json::array({{{"param", "query mbs-session-id"}, {"reason", "not JSON"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}
