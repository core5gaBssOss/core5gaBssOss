// ADR-0643: the AMF's PDU Session Resource Setup codec (nfs/amf/src/pdu_session_resource_setup.cpp,
// TS 38.413 V19.4.0 9.2.1.1 / 9.2.1.2) against the test gNB's own builder and parser
// (tests/integration/ngap_test_gnb.cpp), in both directions, with no NF running.

#include <array>
#include <cstdint>
#include <vector>

#include "ngap_test_gnb.hpp"
#include "pdu_session_resource_setup.hpp"

#include <gtest/gtest.h>

using nf_test::NgapTestGnb;

TEST(AmfPduSessionResourceSetup, RequestCarriesTheIdsTheSliceTheNasPduAndTheTransferUnchanged) {
    amf::ngap::SetupRequestInput in;
    in.amf_ue_id = 7;
    in.ran_ue_id = 3;
    in.pdu_session_id = 5;
    in.s_nssai.sst = 1;
    in.s_nssai.sd = std::array<std::uint8_t, 3>{0x00, 0x00, 0x01};
    in.pdu_session_nas_pdu = {0x7E, 0x02, 0x11, 0x22, 0x33, 0x44, 0x02, 0x68};
    // Opaque to the AMF (9.3.4.1, "transparent to the AMF"): any bytes must come out unchanged.
    in.setup_request_transfer = {0xDE, 0xAD, 0xBE, 0xEF, 0x01};
    const auto bytes = amf::ngap::encode_setup_request(in);
    ASSERT_FALSE(bytes.empty());

    const auto summary = NgapTestGnb::summarize(bytes);
    EXPECT_EQ(summary.outcome, NgapTestGnb::Outcome::Initiating);
    EXPECT_EQ(summary.procedure_code, NgapTestGnb::kProcPduSessionResourceSetup);

    NgapTestGnb::PduSessionResourceSetupRequestInfo out;
    ASSERT_TRUE(NgapTestGnb::parse_pdu_session_resource_setup_request(bytes, out));
    EXPECT_EQ(out.amf_ue_id, 7u);
    EXPECT_EQ(out.ran_ue_id, 3u);
    EXPECT_EQ(out.pdu_session_id, 5);
    EXPECT_EQ(out.sst, 1);
    ASSERT_TRUE(out.sd.has_value());
    EXPECT_EQ(*out.sd, (std::array<std::uint8_t, 3>{0x00, 0x00, 0x01}));
    EXPECT_EQ(out.pdu_session_nas_pdu, in.pdu_session_nas_pdu);
    EXPECT_EQ(out.setup_request_transfer, in.setup_request_transfer);

    // The test gNB's DownlinkNas view of the same message is the item's NAS-PDU.
    NgapTestGnb::DownlinkNas dl;
    ASSERT_TRUE(NgapTestGnb::extract_downlink_nas(bytes, dl));
    EXPECT_TRUE(dl.from_pdu_session_resource_setup);
    EXPECT_EQ(dl.nas_pdu, in.pdu_session_nas_pdu);
}

TEST(AmfPduSessionResourceSetup, WithoutSdTheSliceIsSstOnly) {
    amf::ngap::SetupRequestInput in;
    in.amf_ue_id = 1;
    in.ran_ue_id = 1;
    in.pdu_session_id = 1;
    in.s_nssai.sst = 2;
    in.setup_request_transfer = {0x00};
    NgapTestGnb::PduSessionResourceSetupRequestInfo out;
    ASSERT_TRUE(NgapTestGnb::parse_pdu_session_resource_setup_request(
        amf::ngap::encode_setup_request(in), out));
    EXPECT_EQ(out.sst, 2);
    EXPECT_FALSE(out.sd.has_value());
    EXPECT_TRUE(out.pdu_session_nas_pdu.empty());
}

TEST(AmfPduSessionResourceSetup, ResponseDecodesTheIdsAndEachSessionsTransfer) {
    NgapTestGnb gnb;
    const auto transfer = NgapTestGnb::encode_setup_response_transfer(
        0x0A0B0C0D, std::array<std::uint8_t, 4>{127, 0, 0, 9}, 9);
    ASSERT_FALSE(transfer.empty());
    const auto bytes = gnb.build_pdu_session_resource_setup_response(7, 3, 5, transfer);
    ASSERT_FALSE(bytes.empty());

    const auto rsp = amf::ngap::decode_setup_response(bytes);
    ASSERT_TRUE(rsp.has_value());
    EXPECT_EQ(rsp->amf_ue_id, 7);
    EXPECT_EQ(rsp->ran_ue_id, 3);
    ASSERT_EQ(rsp->setup.size(), 1u);
    EXPECT_EQ(rsp->setup[0].pdu_session_id, 5);
    EXPECT_EQ(rsp->setup[0].transfer, transfer) << "the AMF must relay the transfer unchanged";
    EXPECT_TRUE(rsp->failed.empty());
}

TEST(AmfPduSessionResourceSetup, ResponseDecoderRejectsOtherMessages) {
    amf::ngap::SetupRequestInput in;
    in.setup_request_transfer = {0x00};
    // A Request is an initiatingMessage, not the successfulOutcome of procedure 29.
    EXPECT_FALSE(amf::ngap::decode_setup_response(amf::ngap::encode_setup_request(in)).has_value());
    EXPECT_FALSE(amf::ngap::decode_setup_response({0x00, 0x01}).has_value());
}

TEST(AmfPduSessionResourceSetup, SdParsesSixHexDigitsOnly) {
    EXPECT_EQ(amf::ngap::parse_sd("000001"), (std::array<std::uint8_t, 3>{0x00, 0x00, 0x01}));
    EXPECT_EQ(amf::ngap::parse_sd("A1b2C3"), (std::array<std::uint8_t, 3>{0xA1, 0xB2, 0xC3}));
    EXPECT_FALSE(amf::ngap::parse_sd("00001").has_value());
    EXPECT_FALSE(amf::ngap::parse_sd("00000g").has_value());
}
