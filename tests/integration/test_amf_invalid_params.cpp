// SBI-03 invalidParams, step 2, AMF (ADR-0635; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "\"invalidParams\" attribute shall be included in the \"ProblemDetails\"
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param for a body IE is a JSON Pointer
// (TS29571_CommonData.yaml). The AMF's one NOTE 1 site is N1N2MessageTransfer
// (POST {apiRoot}/ue-contexts/{ueContextId}/n1-n2-messages, N1N2MessageTransferReqData in
// TS29518_Namf_Communication.yaml) with a non-SMS N1 message and no pduSessionId: it names
// "/pduSessionId". With pduSessionId and skipInd the AMF answers 200 N1_MSG_NOT_TRANSFERRED for a
// UE with no NGAP association (ADR-0523), the positive control.
// Real nrf + amf processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token.
#include "sbi_core/http2_client.hpp"
#include "sbi_core/multipart.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "TS26510_CommonData_grp.hpp"
#include "oauth2_enforcement.hpp"
#include "spawn_guard.hpp"

#include <gtest/gtest.h>

namespace {
using nlohmann::json;
constexpr const char* kUeContexts = "https://127.0.0.1:7778/namf-comm/v1/ue-contexts/";
constexpr const char* kUeContextId = "imsi-999700000000061";

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

class AmfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess amf_{AMF_PATH};
    sbi_core::http2::Client client_ = nf_test::oauth2_test_client();
    std::string token_;

    void SetUp() override {
        ASSERT_GT(nrf_.pid(), 0) << "failed to fork nrf";
        ASSERT_GT(amf_.pid(), 0) << "failed to fork amf";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7777)) << "nrf not listening";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7778)) << "amf not listening";
        // Retried until the NRF answers with a token: it may refuse one before the AMF's
        // NFRegister has gone through.
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                token_ = nf_test::oauth2_test_token(client_, "namf-comm", "AMF");
                return !token_.empty();
            },
            std::chrono::seconds(15)))
            << "failed to obtain OAuth2 token from nrf";
        // CreateUEContext (multipart, as test_amf_namf_communication.cpp does), so the transfer
        // gets past its CONTEXT_NOT_FOUND check.
        const json create = sbi_gen::UeContextCreateData{};
        const auto created = send("PUT", std::string(kUeContexts) + kUeContextId, create, {});
        ASSERT_EQ(created.status, 201) << created.body;
    }

    // multipart/related: the jsonData root part plus, when given, one binary N1 part.
    sbi_core::http2::ClientResponse send(const std::string& method,
                                         const std::string& url,
                                         const json& json_data,
                                         std::optional<std::string> n1_content_id) {
        sbi_core::multipart::Part root;
        root.content_type = "application/json";
        root.body = json_data.dump();
        std::vector<sbi_core::multipart::Part> parts{root};
        if (n1_content_id.has_value()) {
            sbi_core::multipart::Part n1;
            n1.content_type = "application/vnd.3gpp.5gnas";
            n1.content_id = *n1_content_id;
            n1.body = std::string("\x2e\x05\x01\xc1", 4);
            parts.push_back(n1);
        }
        const auto encoded = sbi_core::multipart::encode(parts);
        sbi_core::http2::ClientRequest req;
        req.method = method;
        req.url = url;
        req.headers.emplace("content-type", encoded.content_type_header);
        req.headers.emplace("authorization", "Bearer " + token_);
        req.body = encoded.body;
        auto resp = client_.send(req);
        EXPECT_TRUE(resp.has_value()) << url;
        return resp.has_value() ? *resp : sbi_core::http2::ClientResponse{};
    }

    static json sm_transfer() {
        return json{{"n1MessageContainer",
                     {{"n1MessageClass", "SM"}, {"n1MessageContent", {{"contentId", "n1sm"}}}}}};
    }

    std::string transfer_url() const {
        return std::string(kUeContexts) + kUeContextId + "/n1-n2-messages";
    }
};

TEST_F(AmfInvalidParams, SmTransferWithoutPduSessionIdNamesIt) {
    const auto resp = send("POST", transfer_url(), sm_transfer(), "n1sm");
    ASSERT_EQ(resp.status, 400) << resp.body;
    const auto problem = json::parse(resp.body, nullptr, false);
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << resp.body;
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/pduSessionId"}) << resp.body;
}

TEST_F(AmfInvalidParams, PduSessionIdPresentPassesTheCheck) {
    auto body = sm_transfer();
    body["pduSessionId"] = 5;
    body["skipInd"] = true;
    const auto resp = send("POST", transfer_url(), body, "n1sm");
    EXPECT_EQ(resp.status, 200) << resp.body;
    const auto rsp = json::parse(resp.body, nullptr, false);
    EXPECT_EQ(rsp.value("cause", ""), "N1_MSG_NOT_TRANSFERRED") << resp.body;
}

} // namespace
