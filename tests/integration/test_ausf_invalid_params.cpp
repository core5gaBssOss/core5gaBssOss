// SBI-03 invalidParams, step 2, AUSF (ADR-0627; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." The AUSF has one site that answers a NOTE 1 cause itself:
// Nausf_SoRProtection's POST /{supi}/ue-sor without sorHeader (MANDATORY_IE_MISSING). That check
// runs only after the KAUSF lookup, so the SUPI first completes a real 5G-AKA (same flow as
// test_ausf_upu_protection.cpp). Real nrf + udm + udr + ausf processes over TLS 1.3 + mTLS HTTP/2
// with real NRF tokens; the AUSF also needs Valkey (redis_url) and the UDR its Postgres.
#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "TS29509_Nausf_UEAuthentication.hpp"
#include "aka_crypto/hex.hpp"
#include "aka_crypto/kdf.hpp"
#include "aka_crypto/milenage.hpp"
#include "location_path.hpp"
#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

constexpr const char* kAusf = "https://127.0.0.1:7782";
constexpr const char* kServingNetworkName = "5G:mnc070.mcc999.3gppnetwork.org";
// Seeded 5G_AKA subscriber (TS 35.207 Test Set 1 K/OP, ADR-0026).
constexpr const char* kSupi = "imsi-999700000000001";

aka_crypto::Key128 test_k() {
    return *aka_crypto::from_hex<16>("465b5ce8b199b49faa5f0a2ee238a6bc");
}
aka_crypto::Key128 test_opc() {
    const auto op = *aka_crypto::from_hex<16>("cdc202d5123e20f62b6d676ac72cb318");
    return aka_crypto::derive_opc(test_k(), op);
}

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
        "&scope=" + scope + "&targetNfType=AUSF";
    std::string token;
    nf_test::wait_until_ready(
        [&] {
            auto resp = nf_test::send_token_request(req); // succeeds once the AUSF registered
            if (!resp.has_value() || resp->status != 200) {
                return false;
            }
            token = json::parse(resp->body).at("access_token").get<std::string>();
            return true;
        },
        std::chrono::seconds(10));
    return token;
}

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

nf_test::SpawnedProcess spawn_after_nrf(const char* path) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(path);
}

// Real 5G-AKA initiate + confirm, so the AUSF's KausfStore holds a KAUSF for `supi`.
void authenticate(sbi_core::http2::Client& client,
                  const std::string& token,
                  const std::string& supi) {
    sbi_gen::AuthenticationInfo body{};
    body.supiOrSuci = supi;
    body.servingNetworkName = kServingNetworkName;

    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = std::string(kAusf) + "/nausf-auth/v1/ue-authentications";
    req.headers.emplace("content-type", "application/json");
    req.headers.emplace("authorization", "Bearer " + token);
    req.body = json(body).dump();
    auto resp = client.send(req);
    ASSERT_TRUE(resp.has_value());
    ASSERT_EQ(resp->status, 201) << resp->body;
    const auto location_it = resp->headers.find("location");
    ASSERT_NE(location_it, resp->headers.end());

    const auto ctx = json::parse(resp->body).get<sbi_gen::UEAuthenticationCtx>();
    const auto av = ctx.n5gAuthData.get<sbi_gen::Av5gAka>();
    const auto rand = *aka_crypto::from_hex<16>(av.rand);
    const auto autn = *aka_crypto::from_hex<16>(av.autn);

    const auto out = aka_crypto::f2345(test_opc(), test_k(), rand);
    aka_crypto::Sqn sqn{};
    for (size_t i = 0; i < sqn.size(); ++i) {
        sqn[i] = static_cast<uint8_t>(autn[i] ^ out.ak[i]);
    }
    aka_crypto::Amf amf{};
    std::copy(autn.begin() + 6, autn.begin() + 8, amf.begin());
    const auto mac_a = aka_crypto::f1(test_opc(), test_k(), rand, sqn, amf);
    ASSERT_TRUE(std::equal(mac_a.begin(), mac_a.end(), autn.begin() + 8));

    sbi_gen::ConfirmationData confirm{};
    confirm.resStar = aka_crypto::to_hex(
        aka_crypto::derive_res_star(out.ck, out.ik, kServingNetworkName, rand, out.res));

    sbi_core::http2::ClientRequest confirm_req;
    confirm_req.method = "PUT";
    confirm_req.url =
        std::string(kAusf) + nf_test::location_path(location_it->second) + "/5g-aka-confirmation";
    confirm_req.headers.emplace("content-type", "application/json");
    confirm_req.headers.emplace("authorization", "Bearer " + token);
    confirm_req.body = json(confirm).dump();
    auto confirm_resp = client.send(confirm_req);
    ASSERT_TRUE(confirm_resp.has_value());
    ASSERT_EQ(confirm_resp->status, 200) << confirm_resp->body;
}

class AusfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess udm_ = spawn_after_nrf(UDM_PATH);
    nf_test::SpawnedProcess ausf_{AUSF_PATH};
    // ADR-0383: the UDM reads authentication data from the UDR over Nudr.
    nf_test::SpawnedProcess udr_{UDR_PATH};
    sbi_core::http2::Client client_ = make_client();
};

TEST_F(AusfInvalidParams, SorProtectionWithoutSorHeaderNamesSorHeader) {
    ASSERT_TRUE(nf_test::wait_tcp_listening(7777));
    ASSERT_TRUE(nf_test::wait_tcp_listening(7780));
    ASSERT_TRUE(nf_test::wait_tcp_listening(7781));
    ASSERT_TRUE(nf_test::wait_tcp_listening(7782));
    const std::string auth_token = fetch_token("nausf-auth");
    // ADR-0499: the AUSF checks each API's own scope.
    const std::string sor_token = fetch_token("nausf-sorprotection");
    ASSERT_FALSE(auth_token.empty()) << "failed to obtain an nausf-auth token from nrf";
    ASSERT_FALSE(sor_token.empty()) << "failed to obtain an nausf-sorprotection token from nrf";

    authenticate(client_, auth_token, kSupi);

    // TS29509_Nausf_SoRProtection.yaml SorInfo: only ackInd is required, so this body passes
    // parse_json_body and reaches the AUSF's own sorHeader check.
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = std::string(kAusf) + "/nausf-sorprotection/v1/" + kSupi + "/ue-sor";
    req.headers.emplace("content-type", "application/json");
    req.headers.emplace("authorization", "Bearer " + sor_token);
    req.body = json{{"ackInd", false}}.dump();
    auto resp = client_.send(req);
    ASSERT_TRUE(resp.has_value());
    ASSERT_EQ(resp->status, 400) << resp->body;
    const auto problem = json::parse(resp->body);
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    EXPECT_EQ(params_of(problem), std::vector<std::string>{"/sorHeader"}) << problem.dump();
    EXPECT_TRUE(problem.at("invalidParams").at(0).contains("reason")) << problem.dump();
}

} // namespace
