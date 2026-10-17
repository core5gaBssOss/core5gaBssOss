// ADR-0499 phase 4: the 5G-EIR enforces access tokens (oauth2_required, default true). Drives nrf
// and eir as real, separate OS processes: no token -> 401 with a Bearer challenge (TS 29.500
// 6.7.3), a token for another NF type -> 401 invalid_token (aud), a token for another service ->
// 403 insufficient_scope, and the EIR's own scope reaches the handler
// (TS29511_N5g-eir_EquipmentIdentityCheck.yaml, scope n5g-eir-eic).

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

#include "spawn_guard.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

constexpr const char* kStatusUrl =
    "https://127.0.0.1:7792/n5g-eir-eic/v1/equipment-status?pei=imeisv-3510000000000010";

sbi_core::http2::Client make_client() {
    return sbi_core::http2::Client(sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    });
}

std::string fetch_token(sbi_core::http2::Client& client,
                        const std::string& scope,
                        const std::string& target_nf_type) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body = "grant_type=client_credentials&nfInstanceId=test-client&scope=" + scope +
               "&targetNfType=" + target_nf_type;
    const auto resp = client.send(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

sbi_core::http2::ClientResponse get_status(sbi_core::http2::Client& client,
                                           const std::string& token) {
    sbi_core::http2::ClientRequest req;
    req.method = "GET";
    req.url = kStatusUrl;
    if (!token.empty()) {
        req.headers.emplace("authorization", "Bearer " + token);
    }
    auto resp = client.send(req);
    return resp.has_value() ? *resp : sbi_core::http2::ClientResponse{};
}

std::string challenge(const sbi_core::http2::ClientResponse& resp) {
    const auto it = resp.headers.find("www-authenticate");
    return it == resp.headers.end() ? "" : it->second;
}

} // namespace

TEST(EirAuthorization, RequestsNeedATokenForTheEirsOwnServiceAndNfType) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    nf_test::SpawnedProcess eir(EIR_PATH);
    ASSERT_GT(eir.pid(), 0) << "failed to fork eir";

    auto client = make_client();
    sbi_core::http2::ClientResponse resp;
    for (int attempt = 0; attempt < 100 && resp.status == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        resp = get_status(client, "");
    }

    ASSERT_EQ(resp.status, 401) << "no token: " << resp.body;
    EXPECT_EQ(challenge(resp).rfind("Bearer realm=\"", 0), 0U) << challenge(resp);
    EXPECT_EQ(challenge(resp).find("error="), std::string::npos)
        << "a request without a token gets no error attribute (TS 29.500 6.7.3)";

    const auto for_udm = fetch_token(client, "n5g-eir-eic", "UDM");
    ASSERT_FALSE(for_udm.empty());
    resp = get_status(client, for_udm);
    EXPECT_EQ(resp.status, 401) << resp.body;
    EXPECT_NE(challenge(resp).find("error=\"invalid_token\""), std::string::npos)
        << challenge(resp);

    const auto other_service = fetch_token(client, "nudm-sdm", "5G_EIR");
    ASSERT_FALSE(other_service.empty());
    resp = get_status(client, other_service);
    EXPECT_EQ(resp.status, 403) << resp.body;
    EXPECT_NE(challenge(resp).find("error=\"insufficient_scope\""), std::string::npos)
        << challenge(resp);

    const auto token = fetch_token(client, "n5g-eir-eic", "5G_EIR");
    ASSERT_FALSE(token.empty());
    resp = get_status(client, token);
    EXPECT_EQ(resp.status, 200) << resp.body;
    EXPECT_NE(resp.body.find("WHITELISTED"), std::string::npos) << resp.body;
}
