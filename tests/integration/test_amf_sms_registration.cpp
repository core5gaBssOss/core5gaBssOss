// ADR-0486 increment 3a: Registration procedure supporting SMS over NAS (TS 23.502 §4.13.3.1)
// through the real AMF over real NGAP/SCTP + NAS, with a real SMSF.
//
// A UE whose RegistrationRequest carries the 5GS update type "SMS requested" bit (TS 24.501
// §9.11.3.9A) makes the AMF call Nsmsf_SMService_Activate (step 5) BEFORE RegistrationAccept, and
// the Accept carries "SMS over NAS allowed" (§9.11.3.6 octet 3 bit 4) only because the SMSF said
// yes (step 9; TS 24.501 §5.5.1.2.4). A UE that does not ask gets "not allowed" and no SMSF
// context. The Accept is deciphered and MAC-verified with keys the UE side derived itself, and the
// SMSF context is checked on the SMSF itself, so neither assertion is the AMF grading its own work.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "ngap_test_gnb.hpp"
#include "sms_core/sms.hpp"
#include "spawn_guard.hpp"
#include "ue_nas_driver.hpp"

#include <gtest/gtest.h>

namespace {

using nf_test::NgapTestGnb;

constexpr const char* kAmfNgapAddress = "127.0.0.5";
constexpr std::uint16_t kAmfNgapPort = 38412;
constexpr std::uint32_t kGnbId = 0x000044; // clear of the handover/deregistration tests' ids
constexpr const char* kServingNetworkName = "5G:mnc070.mcc999.3gppnetwork.org";

constexpr const char* kAusfProbe =
    "https://127.0.0.1:7782/nausf-auth/v1/ue-authentications/nonexistent/eap-session";
constexpr const char* kUdmProbe = "https://127.0.0.1:7780/nudm-ueau/v1/nonexistent/nonexistent";
constexpr const char* kUdrProbe =
    "https://127.0.0.1:7781/nudr-dr/v1/subscription-data/nonexistent/nonexistent";
constexpr const char* kPcfProbe = "https://127.0.0.1:7783/npcf-am-policy-control/v1/policies/none";
constexpr const char* kSmsfProbe = "https://127.0.0.1:7793/nsmsf-sms/v2/ue-contexts/nonexistent";

struct Lab {
    nf_test::SpawnedProcess nrf{NRF_PATH};
    nf_test::SpawnedProcess udr{UDR_PATH};
    nf_test::SpawnedProcess udm{UDM_PATH};
    nf_test::SpawnedProcess ausf{AUSF_PATH};
    nf_test::SpawnedProcess pcf{PCF_PATH};
    nf_test::SpawnedProcess smsf{SMSF_PATH};
    nf_test::SpawnedProcess amf{AMF_PATH};
};

sbi_core::http2::Client make_client() {
    return sbi_core::http2::Client(sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    });
}

void wait_for_sbi_peers(const std::vector<const char*>& urls, int max_attempts = 200) {
    auto client = make_client();
    for (const char* url : urls) {
        bool reachable = false;
        for (int attempt = 0; attempt < max_attempts && !reachable; ++attempt) {
            sbi_core::http2::ClientRequest req;
            req.method = "GET";
            req.url = url;
            reachable = client.send(req).has_value();
            if (!reachable) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        ASSERT_TRUE(reachable) << "NF never became reachable: " << url;
    }
}

// Registers kTestSupi and returns the deciphered RegistrationAccept (and the UE's keys / AMF id).
void register_ue(NgapTestGnb& gnb,
                 std::uint32_t ran_ue_id,
                 bool sms_requested,
                 std::vector<std::uint8_t>& accept_plain,
                 nf_test::NasKeys* keys_out = nullptr,
                 std::uint64_t* amf_ue_id_out = nullptr) {
    gnb.send_raw(gnb.build_initial_ue_message(
        ran_ue_id, nf_test::build_registration_request(nf_test::kTestSupi, sms_requested)));

    const auto challenge_pdu = gnb.receive_raw();
    ASSERT_FALSE(challenge_pdu.empty());
    NgapTestGnb::DownlinkNas challenge_nas;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(challenge_pdu, challenge_nas));
    const auto challenge = nf_test::parse_authentication_request(challenge_nas.nas_pdu);
    ASSERT_TRUE(challenge.has_value());
    const auto res_star = nf_test::compute_res_star(*challenge, kServingNetworkName);
    ASSERT_TRUE(res_star.has_value());
    gnb.send_raw(gnb.build_uplink_nas_transport(
        challenge_nas.amf_ue_id, ran_ue_id, nf_test::build_authentication_response(*res_star)));

    const auto smc_pdu = gnb.receive_raw();
    ASSERT_FALSE(smc_pdu.empty());
    const auto keys = nf_test::derive_nas_keys(*challenge, nf_test::kTestSupi, kServingNetworkName);
    gnb.send_raw(gnb.build_uplink_nas_transport(
        challenge_nas.amf_ue_id, ran_ue_id, nf_test::build_security_mode_complete(keys, 0)));

    const auto accept_pdu = gnb.receive_raw();
    ASSERT_FALSE(accept_pdu.empty()) << "no RegistrationAccept";
    NgapTestGnb::DownlinkNas accept_nas;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(accept_pdu, accept_nas));
    const auto plain = nf_test::open_secured_downlink(keys, 1, accept_nas.nas_pdu);
    ASSERT_TRUE(plain.has_value()) << "RegistrationAccept MAC did not verify";
    ASSERT_GE(plain->size(), 5u);
    ASSERT_EQ((*plain)[2], 0x42) << "not a RegistrationAccept";
    accept_plain = *plain;
    if (keys_out != nullptr) {
        *keys_out = keys;
    }
    if (amf_ue_id_out != nullptr) {
        *amf_ue_id_out = challenge_nas.amf_ue_id;
    }
}

// The SMSF has a UE SMS context for `supi` iff SMSServiceParameterUpdate (an empty JSON Patch)
// answers 204 rather than 404 -- the SMSF's own state, not the AMF's.
int smsf_context_status(const std::string& supi) {
    auto client = make_client();
    sbi_core::http2::ClientRequest token_req;
    token_req.method = "POST";
    token_req.url = "https://127.0.0.1:7777/oauth2/token";
    token_req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    token_req.body = "grant_type=client_credentials&nfInstanceId=test-client&scope=nsmsf-sms"
                     "&targetNfType=SMSF";
    auto token = client.send(token_req);
    if (!token || token->status != 200) {
        return -1;
    }
    sbi_core::http2::ClientRequest patch;
    patch.method = "PATCH";
    patch.url = "https://127.0.0.1:7793/nsmsf-sms/v2/ue-contexts/" + supi;
    patch.headers.emplace("content-type", "application/json-patch+json");
    patch.headers.emplace(
        "authorization",
        "Bearer " + nlohmann::json::parse(token->body).at("access_token").get<std::string>());
    patch.body = "[]";
    auto resp = client.send(patch);
    return resp ? resp->status : -1;
}

} // namespace

TEST(AmfSmsRegistration, SmsRequestedActivatesTheSmsfAndAcceptSaysSmsAllowed) {
    Lab lab;
    ASSERT_NO_FATAL_FAILURE(
        wait_for_sbi_peers({kUdrProbe, kUdmProbe, kAusfProbe, kPcfProbe, kSmsfProbe}));
    NgapTestGnb gnb;
    ASSERT_TRUE(gnb.connect(kAmfNgapAddress, kAmfNgapPort));
    ASSERT_TRUE(gnb.ng_setup(kGnbId));

    std::vector<std::uint8_t> accept;
    ASSERT_NO_FATAL_FAILURE(register_ue(gnb, 1, /*sms_requested=*/true, accept));
    // 5GS registration result value octet: SMS allowed (bit 4) | 3GPP access (001).
    EXPECT_EQ(accept[4], 0x09);
    EXPECT_EQ(smsf_context_status(nf_test::kTestSupi), 204) << "SMSF holds no UE SMS context";
}

TEST(AmfSmsRegistration, NoSmsRequestedMeansNotAllowedAndNoSmsfContext) {
    Lab lab;
    ASSERT_NO_FATAL_FAILURE(
        wait_for_sbi_peers({kUdrProbe, kUdmProbe, kAusfProbe, kPcfProbe, kSmsfProbe}));
    NgapTestGnb gnb;
    ASSERT_TRUE(gnb.connect(kAmfNgapAddress, kAmfNgapPort));
    ASSERT_TRUE(gnb.ng_setup(kGnbId));

    std::vector<std::uint8_t> accept;
    ASSERT_NO_FATAL_FAILURE(register_ue(gnb, 1, /*sms_requested=*/false, accept));
    EXPECT_EQ(accept[4], 0x01);
    EXPECT_EQ(smsf_context_status(nf_test::kTestSupi), 404);
}

// ADR-0486 increment 3b: MO SMS over NAS end to end (TS 23.502 §4.13.3.3): the UE's UL NAS
// TRANSPORT (SMS) reaches the real SMSF via the real AMF, and the SMSF's CP-ACK (step 2c) and
// CP-DATA/RP-ERROR [SMS-SUBMIT-REPORT] (step 6a) come back as DL NAS TRANSPORTs the UE deciphers
// with its own keys; the UE's closing CP-ACK (step 6c) is then accepted with nothing sent back.
TEST(AmfSmsRegistration, MoSmsRoundTripsThroughAmfAndSmsf) {
    Lab lab;
    ASSERT_NO_FATAL_FAILURE(
        wait_for_sbi_peers({kUdrProbe, kUdmProbe, kAusfProbe, kPcfProbe, kSmsfProbe}));
    NgapTestGnb gnb;
    ASSERT_TRUE(gnb.connect(kAmfNgapAddress, kAmfNgapPort));
    ASSERT_TRUE(gnb.ng_setup(kGnbId));

    constexpr std::uint32_t kRan = 1;
    std::vector<std::uint8_t> accept;
    nf_test::NasKeys keys;
    std::uint64_t amf_ue_id = 0;
    ASSERT_NO_FATAL_FAILURE(register_ue(gnb, kRan, true, accept, &keys, &amf_ue_id));
    ASSERT_EQ(accept[4], 0x09);
    gnb.send_raw(gnb.build_uplink_nas_transport(
        amf_ue_id, kRan, nf_test::build_registration_complete(keys, /*uplink_count=*/1)));

    // CP-DATA (TI 0, from the TI originator) / RP-DATA (MR 0x2A) / SMS-SUBMIT to "46708251358".
    const sms_core::Bytes submit = {0x11, 0x00, 0x0B, 0x91, 0x64, 0x07, 0x28, 0x15,
                                    0x53, 0xF8, 0x00, 0x00, 0xAA, 0x0A, 0xE8, 0x32,
                                    0x9B, 0xFD, 0x46, 0x97, 0xD9, 0xEC, 0x37};
    sms_core::RpMessage rp;
    rp.mti = sms_core::RpMti::DataMsToNetwork;
    rp.message_reference = 0x2A;
    rp.destination_address = {0x91, 0x44, 0x77, 0x58, 0x10, 0x06, 0x50};
    rp.user_data = submit;
    sms_core::CpMessage cp;
    cp.rpdu = sms_core::encode_rp(rp);
    gnb.send_raw(gnb.build_uplink_nas_transport(
        amf_ue_id,
        kRan,
        nf_test::build_ul_nas_transport_sms(keys, /*uplink_count=*/2, sms_core::encode_cp(cp))));

    // Step 2c: CP-ACK, DL NAS COUNT 2.
    const auto first = gnb.receive_raw();
    ASSERT_FALSE(first.empty()) << "no downlink after the uplink SMS";
    NgapTestGnb::DownlinkNas dl1;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(first, dl1));
    const auto plain1 = nf_test::open_secured_downlink(keys, 2, dl1.nas_pdu);
    ASSERT_TRUE(plain1.has_value()) << "DL NAS TRANSPORT MAC did not verify";
    const auto sms1 = nf_test::extract_dl_nas_sms(*plain1);
    ASSERT_TRUE(sms1.has_value()) << "not a DL NAS TRANSPORT carrying SMS";
    EXPECT_EQ(*sms1, (sms_core::Bytes{0x89, 0x04})); // CP-ACK, TI flag 1, TI 0

    // Step 6a: CP-DATA / RP-ERROR (no SMS-IWMSC) with the UE's own RP-MR, DL NAS COUNT 3.
    const auto second = gnb.receive_raw();
    ASSERT_FALSE(second.empty()) << "no RP reply";
    NgapTestGnb::DownlinkNas dl2;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(second, dl2));
    const auto plain2 = nf_test::open_secured_downlink(keys, 3, dl2.nas_pdu);
    ASSERT_TRUE(plain2.has_value());
    const auto sms2 = nf_test::extract_dl_nas_sms(*plain2);
    ASSERT_TRUE(sms2.has_value());
    const auto reply_cp = sms_core::decode_cp(*sms2);
    ASSERT_TRUE(reply_cp.has_value());
    EXPECT_EQ(reply_cp->type, sms_core::CpMessageType::Data);
    const auto reply_rp = sms_core::decode_rp(reply_cp->rpdu);
    ASSERT_TRUE(reply_rp.has_value());
    EXPECT_EQ(reply_rp->mti, sms_core::RpMti::ErrorNetworkToMs);
    EXPECT_EQ(reply_rp->message_reference, 0x2A);

    // Step 6c: the UE's CP-ACK closes the transaction; the AMF relays it and nothing comes back.
    gnb.send_raw(gnb.build_uplink_nas_transport(
        amf_ue_id,
        kRan,
        nf_test::build_ul_nas_transport_sms(
            keys, /*uplink_count=*/3, sms_core::encode_cp(sms_core::make_cp_ack(*reply_cp)))));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
