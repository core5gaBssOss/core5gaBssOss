// nfs/smsf MO SMS over NAS (ADR-0486): TS 23.502 clause 4.13.3.3 steps 2b-2c, 6a-6d, with the
// TS 24.011 / TS 23.040 contents the SMSF must send back.

#include <chrono>

#include "../../nfs/smsf/src/sms_procedures.hpp"

#include <gtest/gtest.h>

using namespace sms_core;

namespace {

const Bytes kSubmit = {0x11, 0x00, 0x0B, 0x91, 0x64, 0x07, 0x28, 0x15, 0x53, 0xF8, 0x00, 0x00,
                       0xAA, 0x0A, 0xE8, 0x32, 0x9B, 0xFD, 0x46, 0x97, 0xD9, 0xEC, 0x37};

Bytes cp_data(std::uint8_t ti, const RpMessage& rp) {
    CpMessage cp;
    cp.ti_value = ti;
    cp.rpdu = encode_rp(rp);
    return encode_cp(cp);
}

RpMessage rp_data(std::uint8_t mr, Bytes tpdu) {
    RpMessage rp;
    rp.mti = RpMti::DataMsToNetwork;
    rp.message_reference = mr;
    rp.destination_address = {0x91, 0x44, 0x77, 0x58, 0x10, 0x06, 0x50};
    rp.user_data = std::move(tpdu);
    return rp;
}

const auto kNow = std::chrono::sys_days{std::chrono::year{2026} / 10 / 14};

} // namespace

TEST(SmsfMoSms, SubmitGetsCpAckThenRpErrorWithSubmitReport) {
    smsf::MoSmsPolicy policy; // #38, 0xC2
    auto out = smsf::process_uplink_sms(cp_data(2, rp_data(0x2A, kSubmit)), policy, kNow);
    ASSERT_TRUE(out.has_value()) << out.error();
    ASSERT_TRUE(out->tpdu.has_value());
    EXPECT_EQ(out->tpdu->type, TpduType::Submit);
    EXPECT_EQ(out->tpdu->destination->digits, "46708251358");
    ASSERT_EQ(out->downlink.size(), 2u);

    // Step 2c: CP-ACK, TI flag set (sent to the TI originator), same TI value.
    EXPECT_EQ(out->downlink[0], (Bytes{0xA9, 0x04}));

    // Step 6a: CP-DATA carrying RP-ERROR (MTI 101) with the same RP-MR and an SMS-SUBMIT-REPORT.
    auto cp = decode_cp(out->downlink[1]);
    ASSERT_TRUE(cp.has_value());
    EXPECT_EQ(cp->type, CpMessageType::Data);
    EXPECT_TRUE(cp->ti_flag);
    EXPECT_EQ(cp->ti_value, 2);
    auto rp = decode_rp(cp->rpdu);
    ASSERT_TRUE(rp.has_value());
    EXPECT_EQ(rp->mti, RpMti::ErrorNetworkToMs);
    EXPECT_EQ(rp->message_reference, 0x2A);
    EXPECT_EQ(rp->cause, 38);
    ASSERT_TRUE(rp->user_data.has_value());
    EXPECT_EQ(classify_tpdu((*rp->user_data)[0], Direction::ScToMs), TpduType::SubmitReport);
    EXPECT_EQ((*rp->user_data)[1], 0xC2); // TP-FCS
    ASSERT_TRUE(out->rp_reply.has_value());
    EXPECT_EQ(*out->rp_reply->user_data, *rp->user_data);
}

TEST(SmsfMoSms, CausesComeFromPolicy) {
    smsf::MoSmsPolicy policy{41, 0xC0};
    auto out = smsf::process_uplink_sms(cp_data(0, rp_data(1, kSubmit)), policy, kNow);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(*out->rp_reply->cause, 41);
    EXPECT_EQ((*out->rp_reply->user_data)[1], 0xC0);
}

TEST(SmsfMoSms, FinalUeCpAckNeedsNoReply) {
    // Step 6c-6d: the UE's closing CP-ACK.
    auto out = smsf::process_uplink_sms(Bytes{0x89, 0x04}, {}, kNow);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->cp.type, CpMessageType::Ack);
    EXPECT_TRUE(out->downlink.empty());
}

TEST(SmsfMoSms, SmmaGetsRpErrorWithoutTpdu) {
    RpMessage smma;
    smma.mti = RpMti::SmmaMsToNetwork;
    smma.message_reference = 5;
    auto out = smsf::process_uplink_sms(cp_data(1, smma), {}, kNow);
    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out->downlink.size(), 2u);
    EXPECT_FALSE(out->tpdu.has_value());
    EXPECT_FALSE(out->rp_reply->user_data.has_value());
}

TEST(SmsfMoSms, BadTpduGetsCause96AndBadRpduOnlyCpAck) {
    auto bad_tpdu = smsf::process_uplink_sms(cp_data(0, rp_data(3, Bytes{0x01, 0x00})), {}, kNow);
    ASSERT_TRUE(bad_tpdu.has_value());
    EXPECT_EQ(*bad_tpdu->rp_reply->cause, 96);
    EXPECT_FALSE(bad_tpdu->tpdu.has_value());

    CpMessage cp;
    cp.rpdu = {0x07}; // reserved MTI, too short
    auto bad_rp = smsf::process_uplink_sms(encode_cp(cp), {}, kNow);
    ASSERT_TRUE(bad_rp.has_value());
    EXPECT_EQ(bad_rp->downlink.size(), 1u); // CP-ACK only
    EXPECT_TRUE(bad_rp->rp_error.has_value());

    EXPECT_FALSE(smsf::process_uplink_sms(Bytes{0x0E, 0x01}, {}, kNow).has_value()); // not SMS PD
}
