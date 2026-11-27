// SBI-03 invalidParams, step 2, TSCTSF (ADR-0638; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "\"invalidParams\" attribute shall be included in the \"ProblemDetails\"
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param for a body IE is a JSON Pointer
// (TS29571_CommonData.yaml). The TSCTSF's four NOTE 1 sites are on the Create of
// Ntsctsf_QoSandTSCAssistance (POST {apiRoot}/tsc-app-sessions, TscAppSessionContextData in
// TS29565_Ntsctsf_QoSandTSCAssistance.yaml): an empty qosReference, the unsupported
// externalGroupId / ueMac / ethFlowInfo / enEthFlowInfo, a ueId without GMEC, and no usable target
// UE. A request past all four reaches the BSF lookup; with no BSF running it is answered 500
// PDU_SESSION_NOT_AVAILABLE, the positive control.
// Real nrf + tsctsf processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token.
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
constexpr const char* kSessions = "https://127.0.0.1:7825/ntsctsf-qos-tscai/v1/tsc-app-sessions";

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

// TscAppSessionContextData with its three required members and an IPv4 target UE.
json session() {
    return json{{"notifUri", "https://127.0.0.1:9/af-notify"},
                {"afId", "af-1"},
                {"qosReference", "qos-gold"},
                {"ueIpAddr", {{"ipv4Addr", "10.45.0.9"}}}};
}

class TsctsfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess tsctsf_{TSCTSF_PATH};
    sbi_core::http2::Client client_ = nf_test::oauth2_test_client();
    std::string token_;

    void SetUp() override {
        ASSERT_GT(nrf_.pid(), 0) << "failed to fork nrf";
        ASSERT_GT(tsctsf_.pid(), 0) << "failed to fork tsctsf";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7777)) << "nrf not listening";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7825)) << "tsctsf not listening";
        // Retried until the NRF answers with a token: it may refuse one before the TSCTSF's
        // NFRegister has gone through.
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                token_ = nf_test::oauth2_test_token(client_, "ntsctsf-qos-tscai", "TSCTSF");
                return !token_.empty();
            },
            std::chrono::seconds(15)))
            << "failed to obtain OAuth2 token from nrf";
    }

    sbi_core::http2::ClientResponse post(const json& body) {
        sbi_core::http2::ClientRequest req;
        req.method = "POST";
        req.url = kSessions;
        req.headers.emplace("authorization", "Bearer " + token_);
        req.headers.emplace("content-type", "application/json");
        req.body = body.dump();
        auto resp = client_.send(req);
        EXPECT_TRUE(resp.has_value()) << body.dump();
        return resp.has_value() ? *resp : sbi_core::http2::ClientResponse{};
    }

    json expect_400(const json& body, const std::string& cause) {
        const auto resp = post(body);
        EXPECT_EQ(resp.status, 400) << resp.body;
        const auto problem = json::parse(resp.body, nullptr, false);
        EXPECT_EQ(problem.value("cause", ""), cause) << resp.body;
        return problem;
    }
};

TEST_F(TsctsfInvalidParams, EmptyQosReferenceIsNamed) {
    auto body = session();
    body["qosReference"] = "";
    const auto problem = expect_400(body, "MANDATORY_IE_MISSING");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/qosReference"}) << problem.dump();
}

TEST_F(TsctsfInvalidParams, UnsupportedEthernetIesAreEachNamed) {
    auto body = session();
    body["ueMac"] = "00-00-5e-00-53-01";
    body["ethFlowInfo"] = json::array({json{{"ethType", "0800"}}});
    const auto problem = expect_400(body, "OPTIONAL_IE_INCORRECT");
    EXPECT_EQ(params_of(problem), (std::vector<std::string>{"/ueMac", "/ethFlowInfo"}))
        << problem.dump();
}

TEST_F(TsctsfInvalidParams, UeIdWithoutGmecNamesUeId) {
    auto body = session();
    body.erase("ueIpAddr");
    body["ueId"] = "msisdn-15550100001";
    const auto problem = expect_400(body, "MANDATORY_IE_INCORRECT");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/ueId"}) << problem.dump();
}

TEST_F(TsctsfInvalidParams, NoTargetUeNamesBothTargetIes) {
    auto body = session();
    body.erase("ueIpAddr");
    const auto problem = expect_400(body, "MANDATORY_IE_MISSING");
    EXPECT_EQ(params_of(problem), (std::vector<std::string>{"/ueIpAddr", "/ueId"}))
        << problem.dump();
}

TEST_F(TsctsfInvalidParams, IpAddrWithoutIpv4NamesIpv4Addr) {
    auto body = session();
    body["ueIpAddr"] = json{{"ipv6Addr", "2001:db8::1"}};
    const auto problem = expect_400(body, "MANDATORY_IE_MISSING");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/ueIpAddr/ipv4Addr"}) << problem.dump();
}

TEST_F(TsctsfInvalidParams, ValidRequestPassesTheChecks) {
    const auto resp = post(session());
    EXPECT_EQ(resp.status, 500) << resp.body;
    const auto problem = json::parse(resp.body, nullptr, false);
    EXPECT_EQ(problem.value("cause", ""), "PDU_SESSION_NOT_AVAILABLE") << resp.body;
}

} // namespace
