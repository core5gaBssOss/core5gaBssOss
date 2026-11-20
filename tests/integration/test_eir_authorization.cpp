// ADR-0499 phase 4: the 5G-EIR enforces access tokens (oauth2_required, default true) on
// N5g-eir_EquipmentIdentityCheck (TS29511_N5g-eir_EquipmentIdentityCheck.yaml, scope
// n5g-eir-eic). Real nrf + eir processes.

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

#include "oauth2_enforcement.hpp"
#include "spawn_guard.hpp"

#include <gtest/gtest.h>

TEST(EirAuthorization, RequestsNeedATokenForTheEirsOwnServiceAndNfType) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    nf_test::SpawnedProcess eir(EIR_PATH);
    ASSERT_GT(eir.pid(), 0) << "failed to fork eir";

    nf_test::expect_oauth2_enforced({
        .method = "GET",
        .url = "https://127.0.0.1:7792/n5g-eir-eic/v1/equipment-status?pei=imeisv-3510000000000010",
        .scope = "n5g-eir-eic",
        .nf_type = "5G_EIR",
        .other_scope = "nudm-sdm",
        .authorized_status = 200,
    });
}

// REVIEW-5ae0ef1 SBI-03 (ADR-0597): a PEI the EIR has no status for is 404 with the cause
// ERROR_EQUIPMENT_UNKNOWN (TS 29.511 6.1.3.2.3.1, table 6.1.5.3-1).
TEST(EirAuthorization, UnknownPeiIs404ErrorEquipmentUnknown) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    nf_test::SpawnedProcess eir(EIR_PATH);
    ASSERT_GT(eir.pid(), 0) << "failed to fork eir";

    auto client = nf_test::oauth2_test_client();
    const nf_test::EnforcedRoute route{
        .method = "GET",
        .url = "https://127.0.0.1:7792/n5g-eir-eic/v1/equipment-status?pei=imeisv-3510000000000099",
    };
    std::string token;
    sbi_core::http2::ClientResponse resp;
    for (int attempt = 0; attempt < 100 && resp.status == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (token.empty()) {
            token = nf_test::oauth2_test_token(client, "n5g-eir-eic", "5G_EIR");
        }
        if (!token.empty()) {
            resp = nf_test::send_to(client, route, token);
        }
    }
    ASSERT_EQ(resp.status, 404) << resp.body;
    EXPECT_EQ(nlohmann::json::parse(resp.body).value("cause", std::string()),
              "ERROR_EQUIPMENT_UNKNOWN")
        << resp.body;
}
