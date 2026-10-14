// ADR-0491: the SEPP's N32 logic (nfs/sepp/src/n32.*) -- TS 23.003 28.2 / 28.5 domains, TS 29.573
// 5.2.2 Security Capability Negotiation at the responding SEPP, 5.3.3.2 N32-f correlation. Also
// writes the request and response bodies for validate_structural_conformance.py
// (TS29573_N32_Handshake.yaml).

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "n32.hpp"

#include <gtest/gtest.h>

namespace {

constexpr const char* kA = "sepp.5gc.mnc001.mcc001.3gppnetwork.org";
constexpr const char* kB = "sepp.5gc.mnc002.mcc001.3gppnetwork.org";

sepp::NegotiationPolicy policy_b() {
    sepp::NegotiationPolicy p;
    p.own_fqdn = kB;
    p.own_mcc = "001";
    p.own_mnc = "02";
    p.allowed_purposes = {"ROAMING", "INTER_PLMN_MOBILITY"};
    p.peers = {sepp::PeerConfig{kA, "001", "01", "https://127.0.0.1:7821", false}};
    return p;
}

sepp::NegotiationPolicy policy_a() {
    sepp::NegotiationPolicy p;
    p.own_fqdn = kA;
    p.own_mcc = "001";
    p.own_mnc = "01";
    p.allowed_purposes = {"ROAMING"};
    p.peers = {sepp::PeerConfig{kB, "001", "02", "https://127.0.0.1:7823", true}};
    return p;
}

void write_sample(const std::string& name, const nlohmann::json& j) {
    const char* dir = std::getenv("SBI_CONFORMANCE_SAMPLES_DIR");
    const std::filesystem::path d = dir != nullptr ? dir : ".";
    std::filesystem::create_directories(d);
    std::ofstream(d / name) << j.dump(2);
}

} // namespace

TEST(SeppN32, HomeNetworkDomainPadsTheMncToThreeDigits) {
    // TS 23.003 28.2's own example: MCC 345, MNC 12.
    EXPECT_EQ(sepp::home_network_domain("345", "12"), "5gc.mnc012.mcc345.3gppnetwork.org");
    EXPECT_EQ(sepp::home_network_domain("001", "001"), "5gc.mnc001.mcc001.3gppnetwork.org");
}

TEST(SeppN32, TheTargetPlmnIsTheTrailingHomeNetworkDomain) {
    EXPECT_EQ(sepp::host_of("https://NRF.5gc.mnc002.mcc001.3gppnetwork.org:7777/nnrf-nfm/v1"),
              "nrf.5gc.mnc002.mcc001.3gppnetwork.org");
    EXPECT_EQ(sepp::network_domain_of_host("nrf.5gc.mnc002.mcc001.3gppnetwork.org"),
              "5gc.mnc002.mcc001.3gppnetwork.org");
    EXPECT_FALSE(sepp::network_domain_of_host("localhost").has_value());
    EXPECT_FALSE(sepp::network_domain_of_host("nrf.5gc.mnc02.mcc001.3gppnetwork.org").has_value());
    EXPECT_TRUE(sepp::is_handshake_id("955cac631f953ed8")); // TS 29.573 5.3.3.3's example
    EXPECT_FALSE(sepp::is_handshake_id("955cac631f953ed"));
}

TEST(SeppN32, ResponderSelectsTlsAndEchoesTheHandshakeId) {
    const auto req = sepp::make_request(policy_a(), policy_a().peers[0], "955cac631f953ed8", false);
    write_sample("sec_negotiate_req.json", nlohmann::json(req));
    const auto d = sepp::negotiate(policy_b(), req, false);
    ASSERT_EQ(d.status, 200) << d.detail;
    ASSERT_TRUE(d.response.has_value());
    write_sample("sec_negotiate_rsp.json", nlohmann::json(*d.response));
    EXPECT_EQ(d.response->selectedSecCapability.value, "TLS");
    EXPECT_EQ(d.response->sender, kB);
    EXPECT_EQ(d.response->n32HandshakeId, "955cac631f953ed8");
    EXPECT_EQ(d.response->n3GppSbiTargetApiRootSupported, true);
    ASSERT_TRUE(d.response->allowedUsagePurpose.has_value());
    EXPECT_EQ(d.response->allowedUsagePurpose->size(), 1U); // A asked for ROAMING only
    ASSERT_TRUE(d.context.has_value());
    EXPECT_EQ(d.context->peer_fqdn, kA);
    EXPECT_EQ(d.context->peer_domain, "5gc.mnc001.mcc001.3gppnetwork.org");
    EXPECT_TRUE(d.context->peer_target_api_root);

    // The initiator keeps the same context from the response.
    const auto ctx =
        sepp::context_from_response(policy_a().peers[0], *d.response, "955cac631f953ed8");
    ASSERT_TRUE(ctx.has_value());
    EXPECT_EQ(ctx->handshake_id, "955cac631f953ed8");
    EXPECT_EQ(ctx->peer_domain, "5gc.mnc002.mcc001.3gppnetwork.org");
}

TEST(SeppN32, ResponderRefusalsUseTheSpecCauses) {
    auto req = sepp::make_request(policy_a(), policy_a().peers[0], std::nullopt, false);

    auto stranger = req;
    stranger.sender = "sepp.5gc.mnc009.mcc001.3gppnetwork.org";
    auto d = sepp::negotiate(policy_b(), stranger, false);
    EXPECT_EQ(d.status, 403);
    EXPECT_EQ(d.cause, "NEGOTIATION_NOT_ALLOWED");

    auto prins_only = req;
    prins_only.supportedSecCapabilityList = {sbi_gen::SecurityCapability{"PRINS"}};
    d = sepp::negotiate(policy_b(), prins_only, false);
    EXPECT_EQ(d.status, 403);
    EXPECT_EQ(d.cause, "NEGOTIATION_NOT_ALLOWED");

    auto purpose = req;
    sbi_gen::IntendedN32Purpose sms;
    sms.usagePurpose.value = "SMS_INTERCONNECT";
    purpose.intendedUsagePurpose = std::vector{sms};
    d = sepp::negotiate(policy_b(), purpose, false);
    EXPECT_EQ(d.status, 403);
    EXPECT_EQ(d.cause, "REQUESTED_PURPOSE_NOT_ALLOWED");

    auto bad_id = req;
    bad_id.n32HandshakeId = "xyz";
    EXPECT_EQ(sepp::negotiate(policy_b(), bad_id, false).status, 400);

    auto wrong_plmn = req;
    wrong_plmn.targetPlmnId = sbi_gen::PlmnId_CommonData{"001", "09"};
    EXPECT_EQ(sepp::negotiate(policy_b(), wrong_plmn, false).status, 400);
}

TEST(SeppN32, CollisionIsWonByTheLexicographicallyPrecedingFqdn) {
    // B receives A's request while B's own request to A is outstanding. A precedes B, so B (whose
    // FQDN does not precede) accepts A's request; A, in the mirror case, refuses with 409.
    const auto from_a = sepp::make_request(policy_a(), policy_a().peers[0], std::nullopt, false);
    EXPECT_EQ(sepp::negotiate(policy_b(), from_a, true).status, 200);

    auto pb = policy_b();
    auto pa = policy_a();
    pa.peers = {sepp::PeerConfig{kB, "001", "02", "https://127.0.0.1:7823", true}};
    const auto from_b =
        sepp::make_request(pb, sepp::PeerConfig{kA, "001", "01", "", true}, std::nullopt, false);
    const auto d = sepp::negotiate(pa, from_b, true);
    EXPECT_EQ(d.status, 409);
    EXPECT_EQ(d.cause, "N32C_EXCHANGE_CAPABILITY_ONGOING");
}

TEST(SeppN32, NoneTearsTheContextDown) {
    const auto req = sepp::make_request(policy_a(), policy_a().peers[0], std::nullopt, true);
    const auto d = sepp::negotiate(policy_b(), req, false);
    EXPECT_EQ(d.status, 200);
    EXPECT_TRUE(d.teardown);
    EXPECT_EQ(d.response->selectedSecCapability.value, "NONE");
    EXPECT_FALSE(d.context.has_value());
}

TEST(SeppN32, CorrelationFollowsClause5332) {
    sepp::Context a;
    a.peer_fqdn = kA;
    a.handshake_id = "955cac631f953ed8";
    a.purposes = {"ROAMING"};
    // One context, a certificate naming only the peer.
    EXPECT_TRUE(sepp::correlate({a}, {"sepp", kA}, "", std::nullopt, std::nullopt).has_value());
    // A certificate not naming the peer.
    EXPECT_FALSE(sepp::correlate({a}, {"sepp", "localhost"}, "", std::nullopt, std::nullopt));
    // A common certificate naming both SEPPs: the Via header decides.
    EXPECT_TRUE(
        sepp::correlate({a}, {kA, kB}, std::string("2.0 SEPP-") + kA, std::nullopt, std::nullopt));
    EXPECT_FALSE(
        sepp::correlate({a}, {kA, kB}, std::string("2.0 SEPP-") + kB, std::nullopt, std::nullopt));
    // Several contexts: by handshake id.
    sepp::Context c = a;
    c.peer_fqdn = "sepp.5gc.mnc003.mcc001.3gppnetwork.org";
    c.handshake_id = "0123456789abcdef";
    const auto by_id = sepp::correlate({a, c}, {}, "", "0123456789ABCDEF", std::nullopt);
    ASSERT_TRUE(by_id.has_value());
    EXPECT_EQ(by_id->peer_fqdn, c.peer_fqdn);
    EXPECT_FALSE(sepp::correlate({a, c}, {}, "", std::nullopt, std::nullopt));
}
