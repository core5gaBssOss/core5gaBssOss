// SBI-03 invalidParams, step 2, 5G-EIR (ADR-0633; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "\"invalidParams\" attribute shall be included in the \"ProblemDetails\"
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param for a query parameter is
// TS29571_CommonData.yaml's "query " plus the name. The EIR's one NOTE 1 site is GetEquipmentStatus
// without its required pei (TS29511_N5g-eir_EquipmentIdentityCheck.yaml): it names "query pei",
// also when only the optional supi / gpsi are given, and with pei present the handler answers 200
// (the ADR-0597 ERROR_EQUIPMENT_UNKNOWN 404 is covered by test_eir_authorization.cpp).
// Real nrf + eir processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token.
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
constexpr const char* kEquipmentStatus = "https://127.0.0.1:7792/n5g-eir-eic/v1/equipment-status";

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

class EirInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess eir_{EIR_PATH};
    sbi_core::http2::Client client_ = nf_test::oauth2_test_client();
    std::string token_;

    void SetUp() override {
        ASSERT_GT(nrf_.pid(), 0) << "failed to fork nrf";
        ASSERT_GT(eir_.pid(), 0) << "failed to fork eir";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7777)) << "nrf not listening";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7792)) << "eir not listening";
        // Retried until the NRF answers with a token: it may refuse one before the EIR's
        // NFRegister has gone through.
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                token_ = nf_test::oauth2_test_token(client_, "n5g-eir-eic", "5G_EIR");
                return !token_.empty();
            },
            std::chrono::seconds(15)))
            << "failed to obtain OAuth2 token from nrf";
    }

    sbi_core::http2::ClientResponse get(const std::string& query) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = std::string(kEquipmentStatus) + query;
        req.headers.emplace("authorization", "Bearer " + token_);
        auto resp = client_.send(req);
        EXPECT_TRUE(resp.has_value()) << query;
        return resp.has_value() ? *resp : sbi_core::http2::ClientResponse{};
    }

    json expect_missing_pei(const std::string& query) {
        const auto resp = get(query);
        EXPECT_EQ(resp.status, 400) << query << ": " << resp.body;
        const auto problem = json::parse(resp.body, nullptr, false);
        EXPECT_EQ(problem.value("cause", ""), "MANDATORY_QUERY_PARAM_MISSING") << query;
        return problem;
    }
};

TEST_F(EirInvalidParams, NoQueryParametersNamesPei) {
    const auto problem = expect_missing_pei("");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"query pei"}) << problem.dump();
}

TEST_F(EirInvalidParams, OnlyOptionalParametersStillNamesPei) {
    const auto problem = expect_missing_pei("?supi=imsi-001010000000001&gpsi=msisdn-001010000001");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"query pei"}) << problem.dump();
}

TEST_F(EirInvalidParams, PeiPresentReachesTheStatusLookup) {
    const auto resp = get("?pei=imeisv-3510000000000010");
    EXPECT_EQ(resp.status, 200) << resp.body;
}

} // namespace
