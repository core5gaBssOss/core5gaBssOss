// ADR-0382: the UDR's project-owned OAM subscriber-provisioning API
// (PUT /oam-provisioning/v1/subscribers/{ueId}), driven over real TLS 1.3 + mTLS against real
// nrf + udr processes and a real PostgreSQL.
//
// What is proven here, and how:
//   * Only the configured mTLS identity (CN=provisioning) may write: another NF's certificate,
//     from the same CA, is refused 403 -- the reason this API does not rely on mTLS alone.
//   * The documents bss/provisioning's UdrAdapter builds are exactly what the UDR's validator
//     accepts (contract test, no process needed).
//   * After a PUT, the subscriber is readable through the STANDARD 3GPP Nudr GETs
//     (provisioned-data am-data/sm-data, authentication-subscription, policy-data am-data/sm-data)
//     -- verified through the read APIs, not by querying the UDR's tables.
//   * Rejection text never echoes the SIM keys it was given.
//
//   * ADR-0569: network data and SIM keys travel separately. A network-data PUT for a subscriber
//     with no credentials is refused 409; the keys-only PUT creates them; the network write then
//     leaves them untouched.
//   * ADR-0569: TMF640 activation (provisioning::tmf640::create_service) against the real
//     orchestration DB and UDR: "active" writes network data only, a pre-active state writes
//     nothing, a second Service for one SUPI and an activation without credentials store nothing.
//
// Not covered here (disclosed in ADR-0382): the full provisioning saga; the provisioning binary's
// HTTP routes (the same create_service/validate_create are called directly); UDM AKA for an
// onboarded SUPI (UDM still reads K/OPc from its own in-memory seed, a separate increment).

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <utility>

#include "oam_provisioning.hpp"
#include "spawn_guard.hpp"
#include "test_tokens.hpp"
#include "tmf640_service.hpp"
#include "udr_adapter.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

// Test-only SIM keys, generated for this file -- not the TS 35.207 test-set values the UDM seeds.
constexpr const char* kTestK = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";
constexpr const char* kTestOpc = "a0b1c2d3e4f5061728394a5b6c7d8e9f";
constexpr const char* kSupi = "imsi-999700000009901";

sbi_core::http2::Client client_as(const std::string& identity) {
    sbi_core::http2::TlsConfig tls{
        .cert_path = std::string(CERTS_DIR) + "/" + identity + "/cert.pem",
        .key_path = std::string(CERTS_DIR) + "/" + identity + "/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

provisioning::UdrAdapterConfig adapter_config() {
    // Mirrors config/provisioning.json's shape; values are this test's own.
    return provisioning::parse_udr_adapter_config(json{
        {"udr_base_url", "https://127.0.0.1:7781"},
        {"serving_plmn_id", "99970"},
        {"authentication",
         {{"method", "5G_AKA"}, {"management_field", "8000"}, {"initial_sqn", "000000000000"}}},
        {"network_profiles",
         {{"default",
           {{"snssais", json::array({{{"sst", 1}, {"sd", "000001"}}})},
            {"dnn", "internet"},
            {"ue_ambr", {{"uplink", "1 Gbps"}, {"downlink", "1 Gbps"}}}}},
          // ADR-0569: a TMF640 ServiceSpecification id (ADR-0558 A1).
          {"rfs-5g-data",
           {{"snssais", json::array({{{"sst", 1}, {"sd", "000001"}}})},
            {"dnn", "internet"},
            {"ue_ambr", {{"uplink", "500 Mbps"}, {"downlink", "2 Gbps"}}}}}}},
    });
}

provisioning::UdrAdapter make_adapter() {
    return provisioning::UdrAdapter(adapter_config(),
                                    sbi_core::http2::TlsConfig{
                                        .cert_path = CERTS_DIR "/provisioning/cert.pem",
                                        .key_path = CERTS_DIR "/provisioning/key.pem",
                                        .ca_path = CERTS_DIR "/ca/ca.crt",
                                    });
}

// A SUPI no earlier run used: the UDR and orchestration DBs persist between local runs, and the
// ADR-0569 tests need a subscriber with no credentials yet.
std::string fresh_supi() {
    static std::atomic<unsigned> n{0};
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto t = static_cast<unsigned long long>(
                       std::chrono::duration_cast<std::chrono::milliseconds>(now).count()) %
                   1000000000ULL;
    return "imsi-99970" + std::to_string(t * 10 + (n++ % 10));
}

std::string orchestration_url() {
    if (const char* url = std::getenv("PROVISIONING_ORCHESTRATION_DATABASE_URL")) {
        return url;
    }
    return "postgresql://postgres@127.0.0.1:5434/orchestration";
}

provisioning::SubscriberSpec spec() {
    return provisioning::SubscriberSpec{
        kSupi, "9997000099", "po-test", provisioning::SimCredentials{kTestK, kTestOpc, ""}};
}

bool wait_reachable(sbi_core::http2::Client& client, const std::string& url, int max_attempts) {
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = url;
        if (client.send(req).has_value()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

std::string fetch_token(sbi_core::http2::Client& client) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body =
        "grant_type=client_credentials&nfInstanceId=" + std::string(nf_test::kTestAfInstanceId) +
        "&scope=nudr-dr&"
        "targetNfType=UDR";
    auto resp = nf_test::send_token_request(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

sbi_core::http2::ClientRequest put(const json& body) {
    sbi_core::http2::ClientRequest req;
    req.method = "PUT";
    req.url = std::string("https://127.0.0.1:7781/oam-provisioning/v1/subscribers/") + kSupi;
    req.headers.emplace("content-type", "application/json");
    req.body = body.dump();
    return req;
}

json get_json(sbi_core::http2::Client& client, const std::string& path, const std::string& token) {
    sbi_core::http2::ClientRequest req;
    req.method = "GET";
    req.url = "https://127.0.0.1:7781/nudr-dr/v2" + path;
    req.headers.emplace("authorization", "Bearer " + token);
    auto resp = client.send(req);
    EXPECT_TRUE(resp.has_value());
    if (!resp.has_value()) {
        return json();
    }
    EXPECT_EQ(resp->status, 200) << path << ": " << resp->body;
    return resp->status == 200 ? json::parse(resp->body) : json();
}

} // namespace

// Contract: the adapter's documents pass the UDR's own validator unchanged.
TEST(UdrOamProvisioningContract, AdapterDocumentsPassUdrValidation) {
    const auto body = provisioning::UdrAdapter(adapter_config(),
                                               sbi_core::http2::TlsConfig{
                                                   .cert_path = CERTS_DIR "/provisioning/cert.pem",
                                                   .key_path = CERTS_DIR "/provisioning/key.pem",
                                                   .ca_path = CERTS_DIR "/ca/ca.crt",
                                               })
                          .build_documents(spec(), nullptr);
    ASSERT_TRUE(body.has_value()) << body.error();
    const auto docs = udr::oam::parse_subscriber_documents(kSupi, *body);
    ASSERT_TRUE(docs.has_value()) << docs.error();
    EXPECT_EQ(docs->serving_plmn_id, "99970");
    // The PCF's smPolicySnssaiData key form ("<sst>-<sd>", nfs/pcf/src/main.cpp).
    EXPECT_TRUE(docs->sm_policy_data["smPolicySnssaiData"].contains("1-000001"));
}

TEST(UdrOamProvisioningContract, RejectionsNameFieldsButNeverEchoKeys) {
    json bad_k = *provisioning::UdrAdapter(adapter_config(),
                                           sbi_core::http2::TlsConfig{
                                               .cert_path = CERTS_DIR "/provisioning/cert.pem",
                                               .key_path = CERTS_DIR "/provisioning/key.pem",
                                               .ca_path = CERTS_DIR "/ca/ca.crt",
                                           })
                      .build_documents(spec(), nullptr);
    bad_k["authenticationSubscription"]["encPermanentKey"] = std::string(kTestK) + "zz";
    const auto r = udr::oam::parse_subscriber_documents(kSupi, bad_k);
    ASSERT_FALSE(r.has_value());
    EXPECT_NE(r.error().find("encPermanentKey"), std::string::npos);
    EXPECT_EQ(r.error().find(kTestK), std::string::npos);
    EXPECT_EQ(r.error().find(kTestOpc), std::string::npos);

    json wrong_supi = bad_k;
    wrong_supi["authenticationSubscription"]["encPermanentKey"] = kTestK;
    wrong_supi["authenticationSubscription"]["supi"] = "imsi-999700000009902";
    EXPECT_FALSE(udr::oam::parse_subscriber_documents(kSupi, wrong_supi).has_value());

    EXPECT_FALSE(
        udr::oam::parse_subscriber_documents("nai-someone@example", json::object()).has_value());
}

TEST(UdrOamProvisioningContract, PeerAllowListMatchesCnOrDnsSan) {
    sbi_core::http2::Request req;
    req.peer_cert_cn = "amf";
    req.peer_cert_dns_names = {"amf", "localhost"};
    EXPECT_FALSE(udr::oam::peer_allowed(req, {"provisioning"}));
    req.peer_cert_dns_names.push_back("provisioning");
    EXPECT_TRUE(udr::oam::peer_allowed(req, {"provisioning"}));
    req = {};
    req.peer_cert_cn = "provisioning";
    EXPECT_TRUE(udr::oam::peer_allowed(req, {"provisioning"}));
    EXPECT_FALSE(udr::oam::peer_allowed(sbi_core::http2::Request{}, {""}));
}

TEST(UdrOamProvisioningIntegration, OnlyProvisioningIdentityWritesAndNudrReadsItBack) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    nf_test::SpawnedProcess udr(UDR_PATH);

    auto prov = client_as("provisioning");
    auto other_nf = client_as("hello-nf");
    ASSERT_TRUE(wait_reachable(other_nf, "https://127.0.0.1:7781/", 200))
        << "udr never became reachable";

    provisioning::UdrAdapter adapter(adapter_config(),
                                     sbi_core::http2::TlsConfig{
                                         .cert_path = CERTS_DIR "/provisioning/cert.pem",
                                         .key_path = CERTS_DIR "/provisioning/key.pem",
                                         .ca_path = CERTS_DIR "/ca/ca.crt",
                                     });
    const json body = *adapter.build_documents(spec(), nullptr);

    // Same CA, wrong identity: refused before the body is even parsed.
    auto refused = other_nf.send(put(body));
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->status, 403) << refused->body;

    // Invalid body from the right identity: 400, keys not echoed.
    json bad = body;
    bad["authenticationSubscription"]["encOpcKey"] = "short";
    auto bad_resp = prov.send(put(bad));
    ASSERT_TRUE(bad_resp.has_value());
    EXPECT_EQ(bad_resp->status, 400);
    EXPECT_EQ(bad_resp->body.find(kTestK), std::string::npos);

    // The adapter's own provision() -- 201 the first time ever, 204 on a persistent DB.
    auto first = adapter.provision(spec());
    ASSERT_TRUE(first.has_value()) << first.error();
    EXPECT_TRUE(first->http_status == 201 || first->http_status == 204);
    EXPECT_EQ(first->profile_key, "default");
    // A resend (saga retry) is a replace: always 204.
    auto again = adapter.provision(spec());
    ASSERT_TRUE(again.has_value()) << again.error();
    EXPECT_EQ(again->http_status, 204);

    // Read back through the standard 3GPP read APIs.
    const std::string token = fetch_token(other_nf);
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";
    const std::string ue = std::string("/subscription-data/") + kSupi;

    const json am = get_json(other_nf, ue + "/99970/provisioned-data/am-data", token);
    EXPECT_EQ(am["nssai"]["defaultSingleNssais"][0]["sst"], 1);
    EXPECT_EQ(am["gpsis"][0], "msisdn-9997000099");
    EXPECT_EQ(am["subscribedUeAmbr"]["downlink"], "1 Gbps");

    const json sm = get_json(other_nf, ue + "/99970/provisioned-data/sm-data", token);
    EXPECT_FALSE(sm.is_null());

    const json auth =
        get_json(other_nf, ue + "/authentication-data/authentication-subscription", token);
    EXPECT_EQ(auth["authenticationMethod"], "5G_AKA");
    EXPECT_EQ(auth["encPermanentKey"], kTestK);
    EXPECT_EQ(auth["sequenceNumber"]["sqn"], "000000000000");

    const json smp =
        get_json(other_nf, std::string("/policy-data/ues/") + kSupi + "/sm-data", token);
    EXPECT_EQ(smp["smPolicySnssaiData"]["1-000001"]["smPolicyDnnData"]["internet"]["dnn"],
              "internet");
    const json amp =
        get_json(other_nf, std::string("/policy-data/ues/") + kSupi + "/am-data", token);
    EXPECT_TRUE(amp.is_object());
}

// ADR-0569 contract: the network-only documents carry no credentials and pass the UDR validator;
// the profile is keyed exactly (no "default" fallback).
TEST(UdrOamProvisioningContract, NetworkDocumentsCarryNoCredentials) {
    auto adapter = make_adapter();
    const auto body = adapter.build_network_documents(kSupi, "9997000099", "rfs-5g-data");
    ASSERT_TRUE(body.has_value()) << body.error();
    EXPECT_FALSE(body->contains("authenticationSubscription"));
    EXPECT_EQ(body->dump().find(kTestK), std::string::npos);
    const auto docs = udr::oam::parse_subscriber_documents(kSupi, *body);
    ASSERT_TRUE(docs.has_value()) << docs.error();
    EXPECT_FALSE(docs->authentication_subscription.has_value());
    EXPECT_EQ(docs->am_data["subscribedUeAmbr"]["downlink"], "2 Gbps");
    EXPECT_FALSE(adapter.build_network_documents(kSupi, "", "po-unknown").has_value());
}

TEST(UdrOamProvisioningIntegration, NetworkDataAndCredentialsTravelSeparately) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    nf_test::SpawnedProcess udr(UDR_PATH);

    auto other_nf = client_as("hello-nf");
    ASSERT_TRUE(wait_reachable(other_nf, "https://127.0.0.1:7781/", 200))
        << "udr never became reachable";
    auto adapter = make_adapter();
    const std::string supi = fresh_supi();
    const provisioning::SimCredentials sim{kTestK, kTestOpc, "00000000002a"};

    // No credentials yet: the network-only write is refused and writes nothing.
    auto early = adapter.provision_network(supi, "9997000098", "rfs-5g-data");
    ASSERT_FALSE(early.has_value());
    EXPECT_TRUE(early.error().no_credentials) << early.error().message;

    // The keys-only route: another identity is refused, provisioning creates then replaces.
    sbi_core::http2::ClientRequest foreign;
    foreign.method = "PUT";
    foreign.url = "https://127.0.0.1:7781/oam-provisioning/v1/subscribers/" + supi +
                  "/authentication-subscription";
    foreign.headers.emplace("content-type", "application/json");
    foreign.body = json{
        {"authenticationMethod", "5G_AKA"},
        {"encPermanentKey", kTestK},
        {"encOpcKey",
         kTestOpc}}.dump();
    auto refused = other_nf.send(foreign);
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->status, 403) << refused->body;

    auto keys = adapter.provision_credentials(supi, sim);
    ASSERT_TRUE(keys.has_value()) << keys.error().message;
    EXPECT_EQ(keys->http_status, 201);
    auto keys_again = adapter.provision_credentials(supi, sim);
    ASSERT_TRUE(keys_again.has_value()) << keys_again.error().message;
    EXPECT_EQ(keys_again->http_status, 204);

    // Now the network write lands: created first, replaced after.
    auto net = adapter.provision_network(supi, "9997000098", "rfs-5g-data");
    ASSERT_TRUE(net.has_value()) << net.error().message;
    EXPECT_EQ(net->http_status, 201);
    EXPECT_EQ(net->profile_key, "rfs-5g-data");
    auto net_again = adapter.provision_network(supi, "9997000098", "rfs-5g-data");
    ASSERT_TRUE(net_again.has_value()) << net_again.error().message;
    EXPECT_EQ(net_again->http_status, 204);

    // Read back through the standard Nudr GETs: the network write kept the keys.
    const std::string token = fetch_token(other_nf);
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";
    const std::string ue = "/subscription-data/" + supi;
    const json am = get_json(other_nf, ue + "/99970/provisioned-data/am-data", token);
    EXPECT_EQ(am["gpsis"][0], "msisdn-9997000098");
    EXPECT_EQ(am["subscribedUeAmbr"]["uplink"], "500 Mbps");
    const json auth =
        get_json(other_nf, ue + "/authentication-data/authentication-subscription", token);
    EXPECT_EQ(auth["encPermanentKey"], kTestK);
    EXPECT_EQ(auth["sequenceNumber"]["sqn"], "00000000002a");
}

TEST(UdrOamProvisioningIntegration, Tmf640ActivationWritesNetworkDataOnlyAndStoresTheService) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    nf_test::SpawnedProcess udr(UDR_PATH);

    auto other_nf = client_as("hello-nf");
    ASSERT_TRUE(wait_reachable(other_nf, "https://127.0.0.1:7781/", 200))
        << "udr never became reachable";
    auto adapter = make_adapter();
    provisioning::tmf640::ServiceStore store(orchestration_url(), 2);
    const auto has_profile = [&adapter](const std::string& id) { return adapter.has_profile(id); };
    const auto body = [](const std::string& supi, const std::string& state) {
        return json{{"category", "RFS"},
                    {"state", state},
                    {"serviceSpecification", {{"id", "rfs-5g-data"}}},
                    {"serviceCharacteristic",
                     json::array({{{"name", "supi"}, {"value", supi}},
                                  {{"name", "msisdn"}, {"value", "9997000097"}}})}}
            .dump();
    };
    const std::string token = fetch_token(other_nf);
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";
    const auto am_status = [&](const std::string& supi) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = "https://127.0.0.1:7781/nudr-dr/v2/subscription-data/" + supi +
                  "/99970/provisioned-data/am-data";
        req.headers.emplace("authorization", "Bearer " + token);
        auto resp = other_nf.send(req);
        return resp.has_value() ? static_cast<int>(resp->status) : -1;
    };

    // 1. "active" for a subscriber whose keys the order flow already wrote.
    const std::string supi = fresh_supi();
    ASSERT_TRUE(adapter.provision_credentials(supi, {kTestK, kTestOpc, ""}).has_value());
    auto v = provisioning::tmf640::validate_create(body(supi, "active"), has_profile);
    ASSERT_TRUE(v.has_value()) << v.error().detail;
    auto created = provisioning::tmf640::create_service(store, adapter, *v);
    ASSERT_TRUE(created.has_value()) << created.error().detail;
    const auto id = created->at("id").get<std::string>();
    EXPECT_EQ(created->at("href"), std::string(provisioning::tmf640::kApiRoot) + "/service/" + id);
    EXPECT_EQ(am_status(supi), 200);
    const auto stored = store.get(id);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, *created);
    EXPECT_EQ(stored->dump().find(kTestK), std::string::npos); // A3
    bool listed = false;
    for (const auto& s : store.list(0, 1000).items) {
        listed = listed || s.at("id") == id;
    }
    EXPECT_TRUE(listed);

    // 2. A second Service for the same SUPI: 409, nothing stored.
    auto twice = provisioning::tmf640::create_service(store, adapter, *v);
    ASSERT_FALSE(twice.has_value());
    EXPECT_EQ(twice.error().status, 409);

    // 3. "active" with no credentials at the UDR: 409, nothing stored, nothing written.
    const std::string keyless = fresh_supi();
    auto kv = provisioning::tmf640::validate_create(body(keyless, "active"), has_profile);
    ASSERT_TRUE(kv.has_value());
    auto refused = provisioning::tmf640::create_service(store, adapter, *kv);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().status, 409);
    EXPECT_EQ(am_status(keyless), 404);
    // Nothing was stored, so the same SUPI can be activated once its keys exist.
    ASSERT_TRUE(adapter.provision_credentials(keyless, {kTestK, kTestOpc, ""}).has_value());
    EXPECT_TRUE(provisioning::tmf640::create_service(store, adapter, *kv).has_value());

    // 4. "reserved": stored, no network write.
    const std::string reserved = fresh_supi();
    auto rv = provisioning::tmf640::validate_create(body(reserved, "reserved"), has_profile);
    ASSERT_TRUE(rv.has_value());
    auto kept = provisioning::tmf640::create_service(store, adapter, *rv);
    ASSERT_TRUE(kept.has_value()) << kept.error().detail;
    EXPECT_EQ(kept->at("state"), "reserved");
    EXPECT_EQ(am_status(reserved), 404);
}
