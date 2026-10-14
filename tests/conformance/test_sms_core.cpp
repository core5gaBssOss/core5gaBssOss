// libs/sms-core (ADR-0486): CP / RP / TP layers of SMS over NAS.
// TS 24.011 clauses 7.2, 7.3, 8.1, 8.2; TS 23.040 clauses 9.1.2.5, 9.2.2.2, 9.2.2.2a, 9.2.2.4,
// 9.2.3.1, 9.2.3.11; TS 24.007 clause 11.2.3.1.

#include <algorithm>
#include <chrono>

#include "sms_core/sms.hpp"

#include <gtest/gtest.h>

using namespace sms_core;

namespace {

// The widely published SMS-SUBMIT example (no SMSC-address prefix): TP-MTI=01 with TP-VPF=10
// (relative), TP-MR=0x00, TP-DA = 11 digits, TON international / NPI E.164 (0x91), "46708251358",
// TP-PID=0, TP-DCS=0, TP-VP=0xAA, TP-UDL=10 septets, TP-UD "hellohello" (GSM 7-bit packed).
const Bytes kSubmit = {0x11, 0x00, 0x0B, 0x91, 0x64, 0x07, 0x28, 0x15, 0x53, 0xF8, 0x00, 0x00,
                       0xAA, 0x0A, 0xE8, 0x32, 0x9B, 0xFD, 0x46, 0x97, 0xD9, 0xEC, 0x37};

Bytes rp_data_mo(std::uint8_t mr, const Bytes& tpdu) {
    RpMessage rp;
    rp.mti = RpMti::DataMsToNetwork;
    rp.message_reference = mr;
    rp.destination_address = {0x91, 0x44, 0x77, 0x58, 0x10, 0x06, 0x50}; // SC +447785016005
    rp.user_data = tpdu;
    return encode_rp(rp);
}

} // namespace

TEST(SmsTp, DecodesPublishedSmsSubmitTpDa) {
    auto t = decode_mo_tpdu(kSubmit);
    ASSERT_TRUE(t.has_value()) << t.error();
    EXPECT_EQ(t->type, TpduType::Submit);
    ASSERT_TRUE(t->message_reference.has_value());
    EXPECT_EQ(*t->message_reference, 0x00);
    ASSERT_TRUE(t->destination.has_value());
    EXPECT_EQ(t->destination->digits, "46708251358");
    EXPECT_EQ(t->destination->type_of_address, 0x91);
    EXPECT_EQ(t->destination->type_of_number(), 0b001); // international
    // raw = Address-Length + TOA + 6 value octets (11 semi-octets, odd -> F filler).
    EXPECT_EQ(t->destination->raw, (Bytes{0x0B, 0x91, 0x64, 0x07, 0x28, 0x15, 0x53, 0xF8}));
}

TEST(SmsTp, AddressEncodeIsTheInverseOfDecode) {
    const auto enc = encode_tp_address("46708251358");
    EXPECT_EQ(enc, (Bytes{0x0B, 0x91, 0x64, 0x07, 0x28, 0x15, 0x53, 0xF8}));
    std::size_t used = 0;
    auto dec = decode_tp_address(enc, used);
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(used, enc.size());
    EXPECT_EQ(dec->digits, "46708251358");
    // Clause 9.1.2.3 extended semi-octets.
    auto star = decode_tp_address(encode_tp_address("*12#"), used);
    ASSERT_TRUE(star.has_value());
    EXPECT_EQ(star->digits, "*12#");
}

TEST(SmsTp, TpMtiIsDirectionDependent) {
    EXPECT_EQ(classify_tpdu(0x00, Direction::MsToSc), TpduType::DeliverReport);
    EXPECT_EQ(classify_tpdu(0x00, Direction::ScToMs), TpduType::Deliver);
    EXPECT_EQ(classify_tpdu(0x01, Direction::MsToSc), TpduType::Submit);
    EXPECT_EQ(classify_tpdu(0x01, Direction::ScToMs), TpduType::SubmitReport);
    EXPECT_EQ(classify_tpdu(0x02, Direction::MsToSc), TpduType::Command);
    EXPECT_EQ(classify_tpdu(0x02, Direction::ScToMs), TpduType::StatusReport);
    EXPECT_EQ(classify_tpdu(0x03, Direction::MsToSc), TpduType::Reserved);
}

TEST(SmsTp, SmsCommandTpDaFollowsPidCtMn) {
    // flags(MTI=10), MR=7, PID=0, CT=1 (cancel status report), MN=3, TP-DA "1234", CDL=0
    const Bytes cmd = {0x02, 0x07, 0x00, 0x01, 0x03, 0x04, 0x81, 0x21, 0x43, 0x00};
    auto t = decode_mo_tpdu(cmd);
    ASSERT_TRUE(t.has_value()) << t.error();
    EXPECT_EQ(t->type, TpduType::Command);
    EXPECT_EQ(*t->message_reference, 7);
    EXPECT_EQ(t->destination->digits, "1234");
    EXPECT_EQ(t->destination->type_of_address, 0x81);
}

TEST(SmsTp, TruncatedTpDaIsRejected) {
    const Bytes bad = {0x01, 0x00, 0x0B, 0x91, 0x64};
    EXPECT_FALSE(decode_mo_tpdu(bad).has_value());
}

// TS 23.040 clause 9.2.2.1: SMS-DELIVER = flags, TP-OA, TP-PID, TP-DCS, TP-SCTS, TP-UDL, TP-UD.
TEST(SmsTp, MtDeliverYieldsTpOaAndTpMms) {
    // flags 0x04: TP-MTI=00, TP-MMS=1 ("no more messages", clause 9.2.3.2); TP-OA "46708251358".
    const Bytes deliver = {0x04, 0x0B, 0x91, 0x64, 0x07, 0x28, 0x15, 0x53, 0xF8, 0x00,
                           0x00, 0x62, 0x01, 0x41, 0x21, 0x43, 0x65, 0x00, 0x0A, 0xE8,
                           0x32, 0x9B, 0xFD, 0x46, 0x97, 0xD9, 0xEC, 0x37};
    auto t = decode_mt_tpdu(deliver);
    ASSERT_TRUE(t.has_value()) << t.error();
    EXPECT_EQ(t->type, TpduType::Deliver);
    ASSERT_TRUE(t->originating.has_value());
    EXPECT_EQ(t->originating->digits, "46708251358");
    EXPECT_EQ(t->originating->raw, Bytes(deliver.begin() + 1, deliver.begin() + 9));
    EXPECT_FALSE(t->recipient.has_value());
    EXPECT_EQ(t->more_messages_to_send, false);

    Bytes more = deliver;
    more[0] = 0x00; // TP-MMS=0: more messages are waiting
    EXPECT_EQ(decode_mt_tpdu(more)->more_messages_to_send, true);
}

// TS 23.040 clause 9.2.2.3: SMS-STATUS-REPORT = flags, TP-MR, TP-RA, TP-SCTS, TP-DT, TP-ST.
TEST(SmsTp, MtStatusReportYieldsTpRaAfterTpMr) {
    const Bytes report = {0x06, 0x2A, 0x04, 0x81, 0x21, 0x43, 0x62, 0x01, 0x41, 0x21, 0x43,
                          0x65, 0x00, 0x62, 0x01, 0x41, 0x21, 0x53, 0x65, 0x00, 0x00};
    auto t = decode_mt_tpdu(report);
    ASSERT_TRUE(t.has_value()) << t.error();
    EXPECT_EQ(t->type, TpduType::StatusReport);
    ASSERT_TRUE(t->recipient.has_value());
    EXPECT_EQ(t->recipient->digits, "1234");
    EXPECT_FALSE(t->originating.has_value());

    const Bytes truncated = {0x00, 0x0B, 0x91, 0x64};
    EXPECT_FALSE(decode_mt_tpdu(truncated).has_value());
}

TEST(SmsRp, RpDataMoRoundTrips) {
    const auto bytes = rp_data_mo(0x2A, kSubmit);
    // MTI 000, MR, OA LV with length 0 (MS->network), DA LV, UD LV.
    EXPECT_EQ(bytes[0], 0x00);
    EXPECT_EQ(bytes[1], 0x2A);
    EXPECT_EQ(bytes[2], 0x00);
    EXPECT_EQ(bytes[3], 0x07);
    auto rp = decode_rp(bytes);
    ASSERT_TRUE(rp.has_value()) << rp.error();
    EXPECT_EQ(rp->mti, RpMti::DataMsToNetwork);
    EXPECT_EQ(rp->message_reference, 0x2A);
    EXPECT_TRUE(rp->originator_address.empty());
    EXPECT_EQ(rp->destination_address.size(), 7u);
    ASSERT_TRUE(rp->user_data.has_value());
    EXPECT_EQ(*rp->user_data, kSubmit);
}

TEST(SmsRp, RpErrorCarriesCauseAndOptionalTpdu) {
    const auto scts =
        encode_scts(std::chrono::sys_days{std::chrono::year{2026} / std::chrono::October / 14} +
                    std::chrono::hours{9} + std::chrono::minutes{5} + std::chrono::seconds{7});
    EXPECT_EQ(scts, (Bytes{0x62, 0x01, 0x41, 0x90, 0x50, 0x70, 0x00}));

    RpMessage err;
    err.mti = RpMti::ErrorNetworkToMs;
    err.message_reference = 0x2A;
    err.cause = 38; // Network out of order (table 8.4 part 1)
    err.user_data = encode_submit_report_error(0xC2, scts);
    const auto bytes = encode_rp(err);
    // MTI 101, MR, RP-Cause LV {len 1, 38}, IEI 0x41, LV TPDU
    EXPECT_EQ(bytes[0], 0x05);
    EXPECT_EQ(bytes[2], 0x01);
    EXPECT_EQ(bytes[3], 38);
    EXPECT_EQ(bytes[4], 0x41);
    auto rp = decode_rp(bytes);
    ASSERT_TRUE(rp.has_value()) << rp.error();
    EXPECT_EQ(rp->mti, RpMti::ErrorNetworkToMs);
    EXPECT_EQ(*rp->cause, 38);
    ASSERT_TRUE(rp->user_data.has_value());
    // SMS-SUBMIT-REPORT for RP-ERROR: MTI 01, FCS, PI=0, SCTS(7).
    EXPECT_EQ(rp->user_data->size(), 10u);
    EXPECT_EQ((*rp->user_data)[0], 0x01);
    EXPECT_EQ((*rp->user_data)[1], 0xC2);
    EXPECT_EQ(classify_tpdu((*rp->user_data)[0], Direction::ScToMs), TpduType::SubmitReport);
}

TEST(SmsRp, RpAckWithoutUserDataIsTwoOctets) {
    RpMessage ack;
    ack.mti = RpMti::AckNetworkToMs;
    ack.message_reference = 9;
    EXPECT_EQ(encode_rp(ack), (Bytes{0x03, 0x09}));
    ack.user_data = encode_submit_report_ack(Bytes(7, 0x00));
    auto rp = decode_rp(encode_rp(ack));
    ASSERT_TRUE(rp.has_value());
    EXPECT_EQ(rp->user_data->size(), 9u);
}

TEST(SmsCp, CpDataWrapsRpduAndAckFlipsTiFlag) {
    CpMessage data;
    data.ti_flag = false; // UE originates the TI
    data.ti_value = 3;
    data.rpdu = rp_data_mo(1, kSubmit);
    const auto bytes = encode_cp(data);
    EXPECT_EQ(bytes[0], 0x39); // TI flag 0, TIO 011, PD 1001
    EXPECT_EQ(bytes[1], 0x01); // CP-DATA
    EXPECT_EQ(bytes[2], data.rpdu.size());

    auto cp = decode_cp(bytes);
    ASSERT_TRUE(cp.has_value()) << cp.error();
    EXPECT_EQ(cp->type, CpMessageType::Data);
    EXPECT_EQ(cp->ti_value, 3);
    EXPECT_EQ(cp->rpdu, data.rpdu);

    const auto ack = encode_cp(make_cp_ack(*cp));
    EXPECT_EQ(ack, (Bytes{0xB9, 0x04})); // TI flag 1 (to the originator), TIO 3, CP-ACK
}

TEST(SmsCp, RejectsWrongPdExtendedTiAndUnknownType) {
    EXPECT_FALSE(decode_cp(Bytes{0x3E, 0x01, 0x00}).has_value()); // PD 1110
    EXPECT_FALSE(decode_cp(Bytes{0x79, 0x04}).has_value());       // TIO 7
    EXPECT_FALSE(decode_cp(Bytes{0x09, 0x02}).has_value());       // type 0x02
    auto err = decode_cp(Bytes{0x09, 0x10, 0x6F});
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->cause, 111);
}

namespace {
std::vector<std::uint8_t> unpack_septets(const Bytes& ud, std::size_t count) {
    std::vector<std::uint8_t> out;
    for (std::size_t s = 0; s < count; ++s) {
        std::uint8_t v = 0;
        for (std::size_t b = 0; b < 7; ++b) {
            const std::size_t bit = s * 7 + b;
            v |= static_cast<std::uint8_t>(((ud[bit / 8] >> (bit % 8)) & 1U) << b);
        }
        out.push_back(v);
    }
    return out;
}
} // namespace

// TS 33.128 clause 7.4.5.2.1.
TEST(SmsRedact, Gsm7SubmitBecomesSpacesWithEveryLengthKept) {
    // "hellohello" in GSM 7 bit default alphabet (h=0x68 e=0x65 l=0x6C o=0x6F) decodes as such.
    const Bytes ud_in(kSubmit.begin() + 14, kSubmit.end());
    EXPECT_EQ(
        unpack_septets(ud_in, 10),
        (std::vector<std::uint8_t>{0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x68, 0x65, 0x6C, 0x6C, 0x6F}));

    auto red = redact_tpdu(kSubmit, Direction::MsToSc);
    ASSERT_TRUE(red.has_value()) << red.error();
    ASSERT_EQ(red->size(), kSubmit.size());
    // Header fields (flags .. TP-UDL) untouched.
    EXPECT_TRUE(std::equal(kSubmit.begin(), kSubmit.begin() + 14, red->begin()));
    const Bytes ud_out(red->begin() + 14, red->end());
    EXPECT_EQ(unpack_septets(ud_out, 10), std::vector<std::uint8_t>(10, 0x20));
}

TEST(SmsRedact, UdhNonControlIesBecomeFillerAndEightBitSmBecomesSpaces) {
    // SMS-SUBMIT, TP-UDHI=1, TP-VPF=00, MR=1, DA "1234", PID 0, DCS 0x04 (8 bit), UDL 14:
    // UDH (UDHL=10): IE 00 concatenation {2A 02 01} (SMS Control), IE 0A text formatting {00 05 01}
    // (EMS Control); then SM "abc".
    const Bytes in = {0x41, 0x01, 0x04, 0x81, 0x21, 0x43, 0x00, 0x04, 0x0E, 0x0A, 0x00, 0x03,
                      0x2A, 0x02, 0x01, 0x0A, 0x03, 0x00, 0x05, 0x01, 'a',  'b',  'c'};
    auto red = redact_tpdu(in, Direction::MsToSc);
    ASSERT_TRUE(red.has_value()) << red.error();
    const Bytes expect = {0x41, 0x01, 0x04, 0x81, 0x21, 0x43, 0x00, 0x04, 0x0E, 0x0A, 0x00, 0x03,
                          0x2A, 0x02, 0x01, 0x26, 0x03, 0x00, 0x00, 0x00, 0x20, 0x20, 0x20};
    EXPECT_EQ(*red, expect);
}

TEST(SmsRedact, Ucs2SmBecomesU0020AndOtherTpdusAreRefused) {
    // SMS-SUBMIT, VPF relative, MR 2, DA "12", PID 0, DCS 0x08 (UCS2), VP 0xAA, UDL 4, "Hi".
    const Bytes in = {0x11, 0x02, 0x02, 0x81, 0x21, 0x00, 0x08, 0xAA, 0x04, 0x00, 'H', 0x00, 'i'};
    auto red = redact_tpdu(in, Direction::MsToSc);
    ASSERT_TRUE(red.has_value()) << red.error();
    EXPECT_EQ(Bytes(red->end() - 4, red->end()), (Bytes{0x00, 0x20, 0x00, 0x20}));
    EXPECT_FALSE(redact_tpdu(Bytes{0x02, 0x07, 0x00, 0x01, 0x03, 0x02, 0x81, 0x21, 0x00},
                             Direction::MsToSc)
                     .has_value()); // SMS-COMMAND
}
