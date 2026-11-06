// REVIEW-5ae0ef1 SUS-02 follow-up (ADR-0565): a write under subscription-data/{ueId} for a user
// the UDR has not provisioned answers 404 USER_NOT_FOUND (TS 29.504 table 6.1.6-2) and stores
// nothing. A SUPI is provisioned when udr_provisioned_data has its row; a GPSI when a provisioned
// subscriber lists it in am_data.gpsis. pp-data-store/{afInstanceId} is the one resource whose
// YAML ueId also allows an External Group Id and "anyUE" (TS29505_Subscription_Data.yaml), so only
// there those two forms are not users. Real nrf + udr processes over TLS 1.3 + mTLS HTTP/2 with
// real NRF tokens; subscribers are onboarded through the real OAM provisioning route.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <utility>

#include "spawn_guard.hpp"
#include "test_tokens.hpp"
#include "udr_test_provisioning.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

constexpr const char* kSubscriptionData = "https://127.0.0.1:7781/nudr-dr/v2/subscription-data/";
// Never onboarded by any test.
constexpr const char* kUnprovisionedSupi = "imsi-999700000009933";
constexpr const char* kProvisionedSupi = "imsi-999700000009932";
constexpr const char* kProvisionedMsisdn = "491701239932";
constexpr const char* kUnlistedGpsi = "msisdn-491701239933";
constexpr const char* kAfInstanceId = "af-adr-0565";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

std::string fetch_token() {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body =
        "grant_type=client_credentials&nfInstanceId=" + std::string(nf_test::kTestAfInstanceId) +
        "&scope=nudr-dr&targetNfType=UDR";
    auto resp = nf_test::send_token_request(req);
    for (int attempt = 0; attempt < 50 && (!resp.has_value() || resp->status != 200); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // the UDR's NFRegister
        resp = nf_test::send_token_request(req);
    }
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

nf_test::SpawnedProcess spawn_udr_after_nrf() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(UDR_PATH);
}

class UdrUnprovisionedWrites : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess udr_ = spawn_udr_after_nrf();
    sbi_core::http2::Client client_ = make_client();
    std::string token_;

    void SetUp() override {
        nf_test::wait_tcp_listening(7777);
        nf_test::wait_tcp_listening(7781);
        token_ = fetch_token();
        ASSERT_FALSE(token_.empty()) << "failed to obtain OAuth2 token from nrf";
    }

    sbi_core::http2::ClientResponse send(const std::string& method,
                                         const std::string& path,
                                         const std::string& body = "",
                                         const std::string& type = "application/json") {
        sbi_core::http2::ClientRequest req;
        req.method = method;
        req.url = std::string(kSubscriptionData) + path;
        req.headers.emplace("authorization", "Bearer " + token_);
        if (!body.empty()) {
            req.headers.emplace("content-type", type);
            req.body = body;
        }
        auto resp = client_.send(req);
        EXPECT_TRUE(resp.has_value()) << method << " " << path;
        return resp.has_value() ? *resp : sbi_core::http2::ClientResponse{};
    }

    void expect_user_not_found(const sbi_core::http2::ClientResponse& resp,
                               const std::string& what) {
        ASSERT_EQ(resp.status, 404) << what << ": " << resp.body;
        EXPECT_EQ(json::parse(resp.body).value("cause", ""), "USER_NOT_FOUND") << what;
    }
};

const json kSmfRegistration = {{"smfInstanceId", "6c9e5f3a-1d2b-4c8e-9a10-0000000000e5"},
                               {"pduSessionId", 5},
                               {"singleNssai", {{"sst", 1}}},
                               {"plmnId", {{"mcc", "999"}, {"mnc", "70"}}}};

TEST_F(UdrUnprovisionedWrites, AnUnprovisionedSupiIsRefusedAcrossTheWriteMethods) {
    const std::string ue = std::string(kUnprovisionedSupi) + "/";
    expect_user_not_found(
        send("PUT", ue + "context-data/smf-registrations/5", kSmfRegistration.dump()),
        "PUT smf-registration");
    expect_user_not_found(send("PATCH",
                               ue + "operator-specific-data",
                               json::array({{{"op", "add"}, {"path", "/x"}, {"value", 1}}}).dump(),
                               "application/json-patch+json"),
                          "PATCH operator-specific-data");
    expect_user_not_found(send("POST", ue + "context-data/ee-subscriptions", "{}"),
                          "POST ee-subscriptions");
    expect_user_not_found(send("DELETE", ue + "context-data/sdm-subscriptions/x"),
                          "DELETE sdm-subscription");

    // A read is not guarded, and it shows nothing was stored.
    const auto list = send("GET", ue + "context-data/smf-registrations");
    ASSERT_EQ(list.status, 200) << list.body;
    EXPECT_TRUE(json::parse(list.body).empty()) << list.body;
}

TEST_F(UdrUnprovisionedWrites, AWriteForAProvisionedSupiIsAccepted) {
    const auto provisioned = nf_test::provision_subscriber(kProvisionedSupi, kProvisionedMsisdn);
    ASSERT_TRUE(provisioned.has_value()) << provisioned.error();

    const std::string path = std::string(kProvisionedSupi) + "/context-data/smf-registrations/5";
    const auto put = send("PUT", path, kSmfRegistration.dump());
    EXPECT_TRUE(put.status == 201 || put.status == 204) << put.status << " " << put.body;
    EXPECT_EQ(send("DELETE", path).status, 204);
}

TEST_F(UdrUnprovisionedWrites, AGpsiIsProvisionedOnlyWhenASubscriberListsIt) {
    const auto provisioned = nf_test::provision_subscriber(kProvisionedSupi, kProvisionedMsisdn);
    ASSERT_TRUE(provisioned.has_value()) << provisioned.error();

    const std::string patch =
        json::array(
            {{{"op", "add"}, {"path", "/supiList"}, {"value", json::array({kProvisionedSupi})}}})
            .dump();
    const auto listed = send("PATCH",
                             std::string("msisdn-") + kProvisionedMsisdn + "/identity-data",
                             patch,
                             "application/json-patch+json");
    EXPECT_EQ(listed.status, 200) << listed.body;
    expect_user_not_found(send("PATCH",
                               std::string(kUnlistedGpsi) + "/identity-data",
                               patch,
                               "application/json-patch+json"),
                          "PATCH identity-data, unlisted GPSI");
}

TEST_F(UdrUnprovisionedWrites, OnlyPpDataStoreTakesAGroupOrAnyUe) {
    const std::string entry = json{{"referenceId", 42}}.dump();
    const std::string resource = std::string("/pp-data-store/") + kAfInstanceId;
    for (const std::string ue : {"anyUE", "extgroupid-adr0565@example.org"}) {
        const auto put = send("PUT", ue + resource, entry);
        EXPECT_TRUE(put.status == 201 || put.status == 204)
            << ue << " " << put.status << " " << put.body;
        EXPECT_EQ(send("DELETE", ue + resource).status, 204) << ue;
    }
    expect_user_not_found(send("PUT", std::string("extgroupid-bad") + resource, entry),
                          "malformed group id");
    expect_user_not_found(send("PUT", std::string(kUnprovisionedSupi) + resource, entry),
                          "unprovisioned SUPI on pp-data-store");
    expect_user_not_found(
        send("PUT", "anyUE/context-data/smf-registrations/5", kSmfRegistration.dump()),
        "anyUE outside pp-data-store");
}

} // namespace
