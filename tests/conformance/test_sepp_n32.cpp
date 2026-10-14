// ADR-0491: the SEPP's N32 logic (nfs/sepp/src/n32.*) -- TS 23.003 28.2 / 28.5 domains, TS 29.573
// 5.2.2 Security Capability Negotiation at the responding SEPP, 5.3.3.2 N32-f correlation. Also
// writes the request and response bodies for validate_structural_conformance.py
// (TS29573_N32_Handshake.yaml).

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "TS29573_SeppTelescopicFqdnMapping.hpp"
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

// ADR-0492: telescopic FQDN (TS 23.003 28.5.2, TS 29.573 5.4).
TEST(SeppN32, TelescopicLabelsAreSingleStableLabels) {
    const std::string fqdn = "nrf.5gc.mnc002.mcc001.3gppnetwork.org";
    const auto label = sepp::telescopic_label(fqdn);
    EXPECT_EQ(label.size(), 20U); // "tel-" + 16 hex
    EXPECT_EQ(label.find('.'), std::string::npos);
    EXPECT_EQ(label, sepp::telescopic_label("NRF.5gc.mnc002.mcc001.3gppnetwork.org")); // case-free
    EXPECT_NE(label, sepp::telescopic_label("nssf.5gc.mnc002.mcc001.3gppnetwork.org"));

    sepp::TelescopicTable table;
    EXPECT_FALSE(table.foreign_for(label).has_value()); // never handed out
    EXPECT_EQ(table.label_for(fqdn), label);
    EXPECT_EQ(table.foreign_for(label), fqdn);

    // The two TelescopicMapping bodies the SEPP returns (5.4.2 / 5.4.3), for the schema check.
    sbi_gen::TelescopicMapping to_label;
    to_label.telescopicLabel = label;
    to_label.seppDomain = kA;
    write_sample("telescopic_mapping_label.json", nlohmann::json(to_label));
    sbi_gen::TelescopicMapping to_fqdn;
    to_fqdn.foreignFqdn = fqdn;
    write_sample("telescopic_mapping_fqdn.json", nlohmann::json(to_fqdn));

    EXPECT_EQ(sepp::telescopic_label_of_host(label + "." + kA, kA), label);
    EXPECT_FALSE(sepp::telescopic_label_of_host(kA, kA).has_value());
    EXPECT_FALSE(sepp::telescopic_label_of_host("a.b." + std::string(kA), kA).has_value());
    EXPECT_FALSE(sepp::telescopic_label_of_host("127.0.0.1", kA).has_value());
}

// ADR-0493: PRINS parameter exchange and the N32-f key hierarchy.
namespace {
std::vector<std::uint8_t> from_hex(const std::string& h) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
    }
    return out;
}
} // namespace

TEST(SeppN32, HkdfExpandMatchesRfc5869TestCase1) {
    // RFC 5869 A.1: PRK, info and the 42-octet OKM (the Expand step alone).
    const auto prk = from_hex("077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5");
    const auto info = from_hex("f0f1f2f3f4f5f6f7f8f9");
    const auto okm = sepp::hkdf_expand(prk, std::string(info.begin(), info.end()), 42);
    EXPECT_EQ(okm,
              from_hex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                       "34007208d5b887185865"));
    // N32-KDF: the info is "N32" || context id || label; keys differ per label and per context.
    const std::vector<std::uint8_t> master(64, 0x42);
    EXPECT_EQ(sepp::n32_kdf(master, "0123456789abcdef", "parallel_request_key", 16),
              sepp::hkdf_expand(master, "N320123456789abcdefparallel_request_key", 16));
    EXPECT_NE(sepp::n32_kdf(master, "0123456789abcdef", "parallel_request_key", 16),
              sepp::n32_kdf(master, "0123456789abcdef", "parallel_response_key", 16));
    EXPECT_NE(sepp::n32_kdf(master, "0123456789abcdef", "parallel_request_key", 16),
              sepp::n32_kdf(master, "fedcba9876543210", "parallel_request_key", 16));
    EXPECT_TRUE(sepp::is_handshake_id(sepp::random_n32f_context_id()));
}

TEST(SeppN32, PrinsIsSelectedByLocalPreference) {
    auto pa = policy_a();
    pa.security_capabilities = {"PRINS", "TLS"};
    auto pb = policy_b();
    pb.security_capabilities = {"PRINS", "TLS"};
    const auto req = sepp::make_request(pa, pa.peers[0], std::nullopt, false);
    auto d = sepp::negotiate(pb, req, false);
    ASSERT_EQ(d.status, 200) << d.detail;
    EXPECT_EQ(d.response->selectedSecCapability.value, "PRINS");
    EXPECT_FALSE(d.response->n3GppSbiTargetApiRootSupported.has_value()); // only with TLS
    EXPECT_EQ(d.context->security, "PRINS");
    pb.security_capabilities = {"TLS", "PRINS"};
    EXPECT_EQ(sepp::negotiate(pb, req, false).response->selectedSecCapability.value, "TLS");
}

TEST(SeppN32, ParameterExchangeNegotiatesCiphersThenPolicy) {
    auto pb = policy_b();
    pb.jwe_cipher_suites = {"A256GCM", "A128GCM"};
    pb.protection_policy.dataTypeEncPolicy = std::vector<sbi_gen::IeType>{{"UEID"}, {"LOCATION"}};
    sepp::Context cap;
    cap.security = "PRINS";
    cap.peer_fqdn = kA;
    const std::vector<std::uint8_t> master(64, 7);

    sbi_gen::SecParamExchReqData req;
    req.n32fContextId = "0123456789abcdef";
    req.jweCipherSuiteList = std::vector<std::string>{"A128GCM"};
    req.jwsCipherSuiteList = std::vector<std::string>{"ES256"};
    // No PRINS capability context: refused.
    EXPECT_EQ(sepp::exchange_params(pb, std::nullopt, std::nullopt, req, master, false).status,
              403);

    auto d = sepp::exchange_params(pb, cap, std::nullopt, req, master, false);
    ASSERT_EQ(d.status, 200) << d.detail;
    write_sample("sec_param_exch_req.json", nlohmann::json(req));
    write_sample("sec_param_exch_rsp.json", nlohmann::json(*d.response));
    EXPECT_EQ(d.response->selectedJweCipherSuite, "A128GCM"); // the only one in common
    EXPECT_EQ(d.response->selectedJwsCipherSuite, "ES256");
    ASSERT_TRUE(d.context.has_value());
    EXPECT_EQ(d.context->peer_id, "0123456789abcdef");
    EXPECT_EQ(d.response->n32fContextId, d.context->local_id);
    EXPECT_EQ(d.context->master_key, master);

    // No JWE suite in common.
    auto none = req;
    none.jweCipherSuiteList = std::vector<std::string>{"A192GCM"};
    EXPECT_EQ(sepp::exchange_params(pb, cap, std::nullopt, none, master, false).cause,
              "NEGOTIATION_NOT_ALLOWED");
    // No exporter output: the key cannot be agreed.
    EXPECT_EQ(sepp::exchange_params(pb, cap, std::nullopt, req, std::nullopt, false).status, 500);

    // 5.2.3.3 on the context just created.
    sbi_gen::SecParamExchReqData pol;
    pol.n32fContextId = "0123456789abcdef";
    pol.protectionPolicyInfo = sbi_gen::ProtectionPolicy{};
    const auto p = sepp::exchange_params(pb, cap, d.context, pol, std::nullopt, false);
    ASSERT_EQ(p.status, 200) << p.detail;
    ASSERT_TRUE(p.response->selProtectionPolicyInfo.has_value());
    EXPECT_EQ(p.response->selProtectionPolicyInfo->dataTypeEncPolicy->size(), 2U);
    EXPECT_TRUE(p.context->peer_policy.has_value());
    // Unknown context id for the policy exchange.
    pol.n32fContextId = "ffffffffffffffff";
    EXPECT_EQ(sepp::exchange_params(pb, cap, d.context, pol, std::nullopt, false).status, 403);
}
