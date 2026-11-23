// SBI-03 invalidParams, step 2, CHF (ADR-0630; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param for a body attribute is a JSON Pointer
// (TS29571_CommonData.yaml). The CHF answers a NOTE 1 cause itself at two sites, both on this
// project's own routes (not Nchf operations):
//   - POST /internal-billing/v1/runs, a required member absent or not a string
//     (MANDATORY_IE_MISSING);
//   - PUT /chf-admin/v1/policy-counters/{policyCounterId}, currentStatus absent or not a string
//     (MANDATORY_IE_INCORRECT).
// Real nrf + chf processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token. The CHF needs, at
// startup, Valkey/Redis (redis_url), Doris FE with the chf_cdr database (direct-insert CDR mode)
// and the rating PostgreSQL on 5434 (config/chf.json); it exits otherwise.
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
constexpr const char* kChf = "https://127.0.0.1:7784";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

// ADR-0499: the scope is the route's apiName, the aud the CHF. The NRF refuses a CHF-targeted
// token until the CHF's NFRegister has landed, so this retries against a deadline.
std::string fetch_token(const std::string& scope) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body =
        "grant_type=client_credentials&nfInstanceId=" + std::string(nf_test::kTestAfInstanceId) +
        "&scope=" + scope + "&targetNfType=CHF";
    std::string token;
    (void)nf_test::wait_until_ready(
        [&] {
            auto resp = nf_test::send_token_request(req);
            if (!resp.has_value() || resp->status != 200) {
                return false;
            }
            token = json::parse(resp->body).at("access_token").get<std::string>();
            return true;
        },
        std::chrono::seconds(10));
    return token;
}

nf_test::SpawnedProcess spawn_chf_after_nrf() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(CHF_PATH);
}

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

class ChfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess chf_ = spawn_chf_after_nrf();
    sbi_core::http2::Client client_ = make_client();

    void SetUp() override {
        ASSERT_TRUE(nf_test::wait_tcp_listening(7777)) << "nrf never listened";
        ASSERT_TRUE(nf_test::wait_tcp_listening(7784, 300))
            << "chf never listened (it needs Valkey, Doris chf_cdr and the rating Postgres)";
    }

    // Sends a JSON body and returns the 400 ProblemDetails, after checking status and cause.
    json expect_400(const std::string& method,
                    const std::string& path,
                    const std::string& token,
                    const json& body,
                    const std::string& cause) {
        sbi_core::http2::ClientRequest req;
        req.method = method;
        req.url = std::string(kChf) + path;
        req.headers.emplace("authorization", "Bearer " + token);
        req.headers.emplace("content-type", "application/json");
        req.body = body.dump();
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

TEST_F(ChfInvalidParams, BillRunNamesTheFirstAbsentMember) {
    const auto token = fetch_token("internal-billing");
    ASSERT_FALSE(token.empty()) << "failed to obtain an internal-billing token from nrf";
    // billingAccountId is present; billNo is the next required member and is absent.
    const auto problem = expect_400("POST",
                                    "/internal-billing/v1/runs",
                                    token,
                                    json{{"billingAccountId", "adr-0630-account"}},
                                    "MANDATORY_IE_MISSING");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/billNo"}) << problem.dump();
    EXPECT_EQ(problem.at("invalidParams").at(0).value("reason", ""), "missing");
}

TEST_F(ChfInvalidParams, BillRunNamesAMemberThatIsNotAString) {
    const auto token = fetch_token("internal-billing");
    ASSERT_FALSE(token.empty()) << "failed to obtain an internal-billing token from nrf";
    const auto problem = expect_400("POST",
                                    "/internal-billing/v1/runs",
                                    token,
                                    json{{"billingAccountId", 630}},
                                    "MANDATORY_IE_MISSING");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/billingAccountId"}) << problem.dump();
    EXPECT_EQ(problem.at("invalidParams").at(0).value("reason", ""), "must be a string");
}

TEST_F(ChfInvalidParams, PolicyCounterWithoutCurrentStatusNamesIt) {
    const auto token = fetch_token("chf-admin");
    ASSERT_FALSE(token.empty()) << "failed to obtain a chf-admin token from nrf";
    const auto problem = expect_400("PUT",
                                    "/chf-admin/v1/policy-counters/adr-0630-counter",
                                    token,
                                    json::object(),
                                    "MANDATORY_IE_INCORRECT");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/currentStatus"}) << problem.dump();
    EXPECT_EQ(problem.at("invalidParams").at(0).value("reason", ""), "missing");
}

TEST_F(ChfInvalidParams, PolicyCounterWithANonStringCurrentStatusNamesIt) {
    const auto token = fetch_token("chf-admin");
    ASSERT_FALSE(token.empty()) << "failed to obtain a chf-admin token from nrf";
    const auto problem = expect_400("PUT",
                                    "/chf-admin/v1/policy-counters/adr-0630-counter",
                                    token,
                                    json{{"currentStatus", 630}},
                                    "MANDATORY_IE_INCORRECT");
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/currentStatus"}) << problem.dump();
    EXPECT_EQ(problem.at("invalidParams").at(0).value("reason", ""), "must be a string");
}

} // namespace
