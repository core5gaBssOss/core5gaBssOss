// ADR-0499 phase 4: the LMF enforces access tokens (oauth2_required, default true) on Nlmf_Location
// and Nlmf_DataExposure (TS29572_Nlmf_Location.yaml / _DataExposure.yaml, scopes nlmf-loc /
// nlmf-dataexposure). Nlmf_Broadcast declares no security scheme and stays mTLS only. Real nrf +
// lmf processes.

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

#include "oauth2_enforcement.hpp"
#include "spawn_guard.hpp"

#include <gtest/gtest.h>

TEST(LmfAuthorization, LocationAndDataExposureNeedATokenAndBroadcastDoesNot) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    nf_test::SpawnedProcess lmf(LMF_PATH);
    ASSERT_GT(lmf.pid(), 0) << "failed to fork lmf";

    // Empty bodies: once authorized, the handlers' own validation answers 400.
    nf_test::expect_oauth2_enforced({
        .method = "POST",
        .url = "https://127.0.0.1:7795/nlmf-loc/v1/cancel-location",
        .body = "{}",
        .scope = "nlmf-loc",
        .nf_type = "LMF",
        .other_scope = "nlmf-dataexposure",
        .authorized_status = 400,
    });
    nf_test::expect_oauth2_enforced({
        .method = "POST",
        .url = "https://127.0.0.1:7795/nlmf-dataexposure/v1/subscriptions",
        .body = "{}",
        .scope = "nlmf-dataexposure",
        .nf_type = "LMF",
        .other_scope = "nlmf-loc",
        .authorized_status = 400,
    });

    auto client = nf_test::oauth2_test_client();
    const auto resp =
        nf_test::send_to(client,
                         {.method = "POST",
                          .url = "https://127.0.0.1:7795/nlmf-broadcast/v1/cipher-key-data",
                          .body = "{}"},
                         "");
    EXPECT_EQ(resp.status, 400) << "Nlmf_Broadcast reaches its handler without a token: "
                                << resp.body;
}

// REVIEW-5ae0ef1 SBI-03 (ADR-0600): CancelLocation for an ldrReference with no location context is
// 403 with the cause LOCATION_SESSION_UNKNOWN (TS 29.572 table 6.1.4.3.2-2, table 6.1.7.3-1).
TEST(LmfAuthorization, CancelLocationForUnknownLdrReferenceIs403LocationSessionUnknown) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    nf_test::SpawnedProcess lmf(LMF_PATH);
    ASSERT_GT(lmf.pid(), 0) << "failed to fork lmf";

    auto client = nf_test::oauth2_test_client();
    const nf_test::EnforcedRoute route{
        .method = "POST",
        .url = "https://127.0.0.1:7795/nlmf-loc/v1/cancel-location",
        .body =
            R"({"hgmlcCallBackURI":"https://127.0.0.1:9999/cb","ldrReference":"ldr-unknown-1"})",
    };
    std::string token;
    sbi_core::http2::ClientResponse resp;
    for (int attempt = 0; attempt < 100 && resp.status == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (token.empty()) {
            token = nf_test::oauth2_test_token(client, "nlmf-loc", "LMF");
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
