// ADR-0499 phase 4: the NSSF enforces access tokens (oauth2_required, default true) on
// Nnssf_NSSelection and Nnssf_NSSAIAvailability (TS29531_Nnssf_NSSelection.yaml /
// _NSSAIAvailability.yaml). Real nrf + nssf processes.

#include "oauth2_enforcement.hpp"
#include "spawn_guard.hpp"

#include <gtest/gtest.h>

TEST(NssfAuthorization, BothServicesNeedATokenForTheNssfAndTheirOwnScope) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    nf_test::SpawnedProcess nssf(NSSF_PATH);
    ASSERT_GT(nssf.pid(), 0) << "failed to fork nssf";

    // An empty body: once authorized, the handler's own validation answers 400.
    nf_test::expect_oauth2_enforced({
        .method = "POST",
        .url = "https://127.0.0.1:7801/nnssf-nssaiavailability/v1/nssai-availability/subscriptions",
        .body = "{}",
        .scope = "nnssf-nssaiavailability",
        .nf_type = "NSSF",
        .other_scope = "nnssf-nsselection",
        .authorized_status = 400,
    });
    // No query parameters: once authorized, the handler answers 400 (nf-type/nf-id are required).
    nf_test::expect_oauth2_enforced({
        .method = "GET",
        .url = "https://127.0.0.1:7801/nnssf-nsselection/v2/network-slice-information",
        .scope = "nnssf-nsselection",
        .nf_type = "NSSF",
        .other_scope = "nnssf-nssaiavailability",
        .authorized_status = 400,
    });
}

// ADR-0592: DELETE of the NSSAI availability of an nfId the NSSF holds nothing for answers 404
// with cause RESOURCE_NOT_FOUND (TS 29.531 table 6.2.3.2.3.3-2, table 6.2.7.3-1).
TEST(NssfAuthorization, NssaiAvailabilityDeleteOfUnknownNfIdIs404ResourceNotFound) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    nf_test::SpawnedProcess nssf(NSSF_PATH);
    ASSERT_GT(nssf.pid(), 0) << "failed to fork nssf";

    const nf_test::EnforcedRoute route{
        .method = "DELETE",
        .url = "https://127.0.0.1:7801/nnssf-nssaiavailability/v1/nssai-availability/"
               "00000000-0000-4000-8000-0000000000aa",
        .scope = "nnssf-nssaiavailability",
        .nf_type = "NSSF",
    };
    auto client = nf_test::oauth2_test_client();
    sbi_core::http2::ClientResponse resp;
    for (int attempt = 0; attempt < 100 && resp.status == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        resp = nf_test::send_to(client, route, "");
    }
    const auto token = nf_test::oauth2_test_token(client, route.scope, route.nf_type);
    resp = nf_test::send_to(client, route, token);
    ASSERT_EQ(resp.status, 404) << resp.body;
    EXPECT_EQ(nlohmann::json::parse(resp.body).value("cause", std::string()), "RESOURCE_NOT_FOUND");
}
