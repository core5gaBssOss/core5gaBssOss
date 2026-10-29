// A UE through the running compose lab (`make lab-up`): NG Setup, Registration (5G-AKA, NAS
// security), RegistrationComplete, PDU Session Establishment and Deregistration over real NGAP/SCTP
// and NAS, to the lab's AMF and every NF behind it. Not part of ctest -- it needs the lab, not
// spawned NFs:
//
//   LAB_AMF_N2=<AMF container IP> ./build/tests/integration/lab_smoke_tests
//
// The AMF container IP: `docker inspect docker-amf-1 --format '{{range .NetworkSettings.Networks}}
// {{.IPAddress}}{{end}}'`. Skipped when LAB_AMF_N2 is unset.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "ngap_test_gnb.hpp"
#include "ue_nas_driver.hpp"

#include <gtest/gtest.h>

namespace {

using nf_test::NgapTestGnb;

constexpr std::uint16_t kN2Port = 38412;
constexpr std::uint32_t kGnbId = 0x000044;
constexpr const char* kServingNetworkName = "5G:mnc070.mcc999.3gppnetwork.org";
constexpr const char* kDnn = "internet";
constexpr std::uint8_t kSst = 1;
constexpr std::uint32_t kSd = 0x000001;

std::string hex(const std::vector<std::uint8_t>& b) {
    std::string out;
    char buf[4];
    for (const auto x : b) {
        std::snprintf(buf, sizeof(buf), "%02x", x);
        out += buf;
    }
    return out;
}

TEST(LabSmoke, AUeRegistersEstablishesAPduSessionAndDeregisters) {
    const char* amf = std::getenv("LAB_AMF_N2");
    if (amf == nullptr || *amf == '\0') {
        GTEST_SKIP() << "LAB_AMF_N2 not set -- this test drives the running compose lab";
    }
    NgapTestGnb gnb;
    ASSERT_TRUE(gnb.connect(amf, kN2Port)) << "no SCTP association to the lab AMF at " << amf;
    ASSERT_TRUE(gnb.ng_setup(kGnbId)) << "NG Setup refused";
    std::printf("[lab] NG Setup accepted by the AMF at %s\n", amf);

    constexpr std::uint32_t kRanUeId = 1;
    gnb.send_raw(gnb.build_initial_ue_message(
        kRanUeId, nf_test::build_registration_request(nf_test::kTestSupi)));
    const auto challenge_pdu = gnb.receive_raw();
    ASSERT_FALSE(challenge_pdu.empty()) << "the AMF sent nothing after the RegistrationRequest";
    NgapTestGnb::DownlinkNas challenge_nas;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(challenge_pdu, challenge_nas));
    const auto challenge = nf_test::parse_authentication_request(challenge_nas.nas_pdu);
    ASSERT_TRUE(challenge.has_value()) << "expected an AuthenticationRequest";
    const auto res_star = nf_test::compute_res_star(*challenge, kServingNetworkName);
    ASSERT_TRUE(res_star.has_value()) << "AUTN did not authenticate the network";
    std::printf("[lab] 5G-AKA challenge received; AUTN verified, RES* computed\n");
    gnb.send_raw(gnb.build_uplink_nas_transport(
        challenge_nas.amf_ue_id, kRanUeId, nf_test::build_authentication_response(*res_star)));

    const auto smc_pdu = gnb.receive_raw();
    NgapTestGnb::DownlinkNas smc_nas;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(smc_pdu, smc_nas)) << "no SecurityModeCommand";
    ASSERT_GE(smc_nas.nas_pdu.size(), 10u);
    ASSERT_EQ(smc_nas.nas_pdu[9], 0x5D) << "expected a SecurityModeCommand (AUSF accepted RES*)";
    const auto keys = nf_test::derive_nas_keys(*challenge, nf_test::kTestSupi, kServingNetworkName);
    gnb.send_raw(gnb.build_uplink_nas_transport(
        challenge_nas.amf_ue_id, kRanUeId, nf_test::build_security_mode_complete(keys, 0)));

    const auto accept_pdu = gnb.receive_raw();
    NgapTestGnb::DownlinkNas accept_nas;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(accept_pdu, accept_nas))
        << "no RegistrationAccept";
    const auto accept = nf_test::open_secured_downlink(keys, 1, accept_nas.nas_pdu);
    ASSERT_TRUE(accept.has_value()) << "RegistrationAccept's MAC did not verify";
    ASSERT_EQ((*accept)[2], 0x42) << "expected a RegistrationAccept";
    const auto guti = nf_test::extract_guti_from_registration_accept(*accept);
    ASSERT_TRUE(guti.has_value());
    std::printf("[lab] RegistrationAccept (NAS security active), 5G-GUTI %s\n", hex(*guti).c_str());
    gnb.send_raw(gnb.build_uplink_nas_transport(
        challenge_nas.amf_ue_id, kRanUeId, nf_test::build_registration_complete(keys, 1)));

    constexpr std::uint8_t kPsi = 5;
    gnb.send_raw(gnb.build_uplink_nas_transport(
        challenge_nas.amf_ue_id,
        kRanUeId,
        nf_test::build_pdu_session_establishment_request(keys, 2, kPsi, 1, kDnn, kSst, kSd)));
    const auto sm_pdu = gnb.receive_raw();
    NgapTestGnb::DownlinkNas sm_nas;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(sm_pdu, sm_nas))
        << "no answer to the PDU request";
    const auto sm_plain = nf_test::open_secured_downlink(keys, 2, sm_nas.nas_pdu);
    ASSERT_TRUE(sm_plain.has_value());
    const auto n1_sm = nf_test::extract_dl_nas_payload_container(*sm_plain);
    ASSERT_TRUE(n1_sm.has_value());
    ASSERT_GE(n1_sm->size(), 4u);
    EXPECT_EQ((*n1_sm)[3], 0xC2) << "expected PDU SESSION ESTABLISHMENT ACCEPT, got 5GSM type "
                                 << static_cast<int>((*n1_sm)[3]);
    std::printf("[lab] PDU SESSION ESTABLISHMENT ACCEPT, PDU session %u, DNN %s: %s\n",
                kPsi,
                kDnn,
                hex(*n1_sm).c_str());
    if (n1_sm->size() >= 27 && (*n1_sm)[20] == 0x29) {
        std::printf("[lab] UE IPv4 address %u.%u.%u.%u\n",
                    (*n1_sm)[23],
                    (*n1_sm)[24],
                    (*n1_sm)[25],
                    (*n1_sm)[26]);
    } else {
        std::printf("[lab] no PDU address IE (this lab build predates ADR-0525)\n");
    }

    gnb.send_raw(gnb.build_uplink_nas_transport(
        challenge_nas.amf_ue_id,
        kRanUeId,
        nf_test::build_deregistration_request(keys, 3, *guti, /*switch_off=*/false)));
    const auto dereg_pdu = gnb.receive_raw();
    NgapTestGnb::DownlinkNas dereg_nas;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(dereg_pdu, dereg_nas))
        << "no DeregistrationAccept";
    const auto dereg = nf_test::open_secured_downlink(keys, 3, dereg_nas.nas_pdu);
    ASSERT_TRUE(dereg.has_value());
    EXPECT_EQ((*dereg)[2], 0x46) << "expected a DeregistrationAccept";
    std::printf("[lab] DeregistrationAccept\n");
    const auto release = gnb.receive_raw();
    if (!release.empty()) {
        gnb.send_raw(gnb.build_ue_context_release_complete(challenge_nas.amf_ue_id, kRanUeId));
    }
}

} // namespace
