// ADR-0499 phase 4: the GMLC enforces access tokens (oauth2_required, default true) on
// Ngmlc_Location (TS29515_Ngmlc_Location.yaml, scope ngmlc-loc). Real nrf + gmlc processes.

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

#include "oauth2_enforcement.hpp"
#include "spawn_guard.hpp"

#include <gtest/gtest.h>

TEST(GmlcAuthorization, NgmlcLocationNeedsATokenForTheGmlcAndItsService) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    nf_test::SpawnedProcess gmlc(GMLC_PATH);
    ASSERT_GT(gmlc.pid(), 0) << "failed to fork gmlc";

    // An empty body: once authorized, the handler's own validation answers 400.
    nf_test::expect_oauth2_enforced({
        .method = "POST",
        .url = "https://127.0.0.1:7794/ngmlc-loc/v1/cancel-location",
        .body = "{}",
        .scope = "ngmlc-loc",
        .nf_type = "GMLC",
        .other_scope = "nlmf-loc",
        .authorized_status = 400,
    });
}

// REVIEW-5ae0ef1 SBI-03 (ADR-0599): CancelLocation for an ldrReference with no location context is
// 403 with the cause LOCATION_SESSION_UNKNOWN (TS 29.515 table 6.1.3.3.2-2, table 6.1.6.3-1).
TEST(GmlcAuthorization, CancelLocationForUnknownLdrReferenceIs403LocationSessionUnknown) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    nf_test::SpawnedProcess gmlc(GMLC_PATH);
    ASSERT_GT(gmlc.pid(), 0) << "failed to fork gmlc";

    auto client = nf_test::oauth2_test_client();
    const nf_test::EnforcedRoute route{
        .method = "POST",
        .url = "https://127.0.0.1:7794/ngmlc-loc/v1/cancel-location",
        .body =
            R"({"hgmlcCallBackUri":"https://127.0.0.1:9999/cb","ldrReference":"ldr-unknown-1"})",
    };
    std::string token;
    sbi_core::http2::ClientResponse resp;
    for (int attempt = 0; attempt < 100 && resp.status == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (token.empty()) {
            token = nf_test::oauth2_test_token(client, "ngmlc-loc", "GMLC");
        }
        if (!token.empty()) {
            resp = nf_test::send_to(client, route, token);
        }
    }
    ASSERT_EQ(resp.status, 403) << resp.body;
    EXPECT_EQ(nlohmann::json::parse(resp.body).value("cause", std::string()),
              "LOCATION_SESSION_UNKNOWN")
        << resp.body;
}
