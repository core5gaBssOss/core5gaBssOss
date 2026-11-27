// SBI-03 invalidParams, step 2, GMLC (ADR-0634; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "\"invalidParams\" attribute shall be included in the \"ProblemDetails\"
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param for a body IE is a JSON Pointer
// (TS29571_CommonData.yaml). The GMLC's one NOTE 1 site is LocationUpdate
// (POST {apiRoot}/location-update, LocUpdateData in TS29515_Ngmlc_Location.yaml) with neither supi
// nor gpsi: it names "/supi" and "/gpsi". With supi present the handler answers 204.
// Real nrf + gmlc processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token.
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
constexpr const char* kLocationUpdate = "https://127.0.0.1:7794/ngmlc-loc/v1/location-update";

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

// LocUpdateData with every required IE (TS29515_Ngmlc_Location.yaml) and no UE identity.
json loc_update_without_ue_id() {
    return json{{"locationRequestType", "MO_LR"},
                {"locationEstimate", {{"shape", "POINT"}, {"point", {{"lat", 1.0}, {"lon", 2.0}}}}},
                {"ageOfLocationEstimate", 5},
                {"accuracyFulfilmentIndicator", "REQUESTED_ACCURACY_FULFILLED"},
                {"lcsQosClass", "BEST_EFFORT"}};
}

class GmlcInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess gmlc_{GMLC_PATH};
    sbi_core::http2::Client client_ = nf_test::oauth2_test_client();
    std::string token_;

    void SetUp() override {
        ASSERT_GT(nrf_.pid(), 0) << "failed to fork nrf";
        ASSERT_GT(gmlc_.pid(), 0) << "failed to fork gmlc";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7777)) << "nrf not listening";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7794)) << "gmlc not listening";
        // Retried until the NRF answers with a token: it may refuse one before the GMLC's
        // NFRegister has gone through.
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                token_ = nf_test::oauth2_test_token(client_, "ngmlc-loc", "GMLC");
                return !token_.empty();
            },
            std::chrono::seconds(15)))
            << "failed to obtain OAuth2 token from nrf";
    }

    sbi_core::http2::ClientResponse post(const json& body) {
        sbi_core::http2::ClientRequest req;
        req.method = "POST";
        req.url = kLocationUpdate;
        req.headers.emplace("authorization", "Bearer " + token_);
        req.headers.emplace("content-type", "application/json");
        req.body = body.dump();
        auto resp = client_.send(req);
        EXPECT_TRUE(resp.has_value()) << body.dump();
        return resp.has_value() ? *resp : sbi_core::http2::ClientResponse{};
    }
};

TEST_F(GmlcInvalidParams, NeitherSupiNorGpsiNamesBoth) {
    const auto resp = post(loc_update_without_ue_id());
    ASSERT_EQ(resp.status, 400) << resp.body;
    const auto problem = json::parse(resp.body, nullptr, false);
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << resp.body;
    EXPECT_EQ(params_of(problem), (std::vector<std::string>{"/supi", "/gpsi"})) << resp.body;
}

TEST_F(GmlcInvalidParams, SupiPresentIsStored) {
    auto body = loc_update_without_ue_id();
    body["supi"] = "imsi-001010000000001";
    const auto resp = post(body);
    EXPECT_EQ(resp.status, 204) << resp.body;
}

} // namespace
