// nfs/smsf MT SMS over NAS (ADR-0486 increment 4): Nsmsf_SMService_MtForwardSm payload handling
// and the SMSF's MT transaction, TS 23.502 clause 4.13.3.6 steps 5a-6d, TS 23.540 clauses 5.1.2 /
// 5.1.5, TS 24.011 clauses 3.2, 5.3.4 and 8.

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "../../nfs/smsf/src/mt_sms.hpp"

#include <gtest/gtest.h>

using namespace sms_core;

namespace {

// SMS-DELIVER, TP-MMS=1, TP-OA "46708251358", "hellohello" (TS 23.040 clause 9.2.2.1).
const Bytes kDeliver = {0x04, 0x0B, 0x91, 0x64, 0x07, 0x28, 0x15, 0x53, 0xF8, 0x00,
                        0x00, 0x62, 0x01, 0x41, 0x21, 0x43, 0x65, 0x00, 0x0A, 0xE8,
                        0x32, 0x9B, 0xFD, 0x46, 0x97, 0xD9, 0xEC, 0x37};

Bytes gmsc_payload(const Bytes& tpdu, RpMti mti = RpMti::DataNetworkToMs) {
    RpMessage rp;
    rp.mti = mti;
    rp.message_reference = 0x11;
    rp.originator_address = {0x91, 0x44, 0x77, 0x58, 0x10, 0x06, 0x50}; // SC +447785016005
    rp.user_data = tpdu;
    CpMessage cp;
    cp.ti_value = 3; // the SMS-GMSC's own TI
    cp.rpdu = encode_rp(rp);
    return encode_cp(cp);
}

// The UE's side: CP-ACK, then CP-DATA(RP-ACK [SMS-DELIVER-REPORT]) on the SMSF's TI.
CpMessage ue_cp(std::uint8_t ti, CpMessageType type, Bytes rpdu = {}) {
    CpMessage cp;
    cp.ti_flag = true; // sent to the side that originated the TI (TS 24.007 table 11.3)
    cp.ti_value = ti;
    cp.type = type;
    cp.rpdu = std::move(rpdu);
    return cp;
}

RpMessage ue_rp_ack() {
    RpMessage rp;
    rp.mti = RpMti::AckMsToNetwork;
    rp.message_reference = 0x11;
    rp.user_data = Bytes{0x00, 0x00}; // SMS-DELIVER-REPORT for RP-ACK, TP-PI = 0 (9.2.2.1a)
    return rp;
}

} // namespace

TEST(SmsfMtSms, ParseAcceptsCpDataRpDataDeliverOnly) {
    auto ok = smsf::parse_mt_forward_payload(gmsc_payload(kDeliver));
    ASSERT_TRUE(ok.has_value()) << ok.error();
    EXPECT_EQ(ok->cp.ti_value, 3);
    EXPECT_EQ(ok->rp.message_reference, 0x11);
    EXPECT_EQ(ok->tpdu.type, TpduType::Deliver);
    EXPECT_EQ(ok->tpdu.originating->digits, "46708251358");

    // An MS -> network RP-DATA, an SMS-SUBMIT-REPORT TPDU, a CP-ACK, junk: SMS_PAYLOAD_ERROR.
    EXPECT_FALSE(
        smsf::parse_mt_forward_payload(gmsc_payload(kDeliver, RpMti::DataMsToNetwork)).has_value());
    EXPECT_FALSE(smsf::parse_mt_forward_payload(
                     gmsc_payload(Bytes{0x01, 0x00, 0x62, 0x01, 0x41, 0x21, 0x43, 0x65, 0x00}))
                     .has_value());
    CpMessage ack;
    ack.type = CpMessageType::Ack;
    EXPECT_FALSE(smsf::parse_mt_forward_payload(encode_cp(ack)).has_value());
    EXPECT_FALSE(smsf::parse_mt_forward_payload(Bytes{0xFF, 0xFF}).has_value());
}

TEST(SmsfMtSms, DownlinkUsesTheSmsfsOwnTiAndTheReportAnswersTheGmscsTi) {
    auto req = smsf::parse_mt_forward_payload(gmsc_payload(kDeliver));
    ASSERT_TRUE(req.has_value());

    auto down = decode_cp(smsf::make_mt_cp_data(*req, 5));
    ASSERT_TRUE(down.has_value());
    EXPECT_FALSE(down->ti_flag); // the SMSF originates the CM transaction towards the UE
    EXPECT_EQ(down->ti_value, 5);
    EXPECT_EQ(down->type, CpMessageType::Data);
    EXPECT_EQ(down->rpdu, req->cp.rpdu); // RP-DATA relayed as received

    const Bytes ue_rpdu = encode_rp(ue_rp_ack());
    auto report = decode_cp(smsf::make_delivery_report_payload(*req, ue_rpdu));
    ASSERT_TRUE(report.has_value());
    EXPECT_TRUE(report->ti_flag); // reply direction of the SMS-GMSC's TI 3
    EXPECT_EQ(report->ti_value, 3);
    EXPECT_EQ(report->rpdu, ue_rpdu);
}

TEST(SmsfMtSms, TransactionCompletesOnTheUesDeliveryReport) {
    smsf::MtTransactions mt({std::chrono::seconds(5), 4});
    std::thread ue;
    auto result = mt.run("imsi-001010000000001", [&](std::uint8_t ti) {
        mt.downlink_result("imsi-001010000000001", ti, true);
        ue = std::thread([&mt, ti] {
            // A message on another TI, or an MO one (TI flag 0), is not this transaction's.
            EXPECT_FALSE(
                mt.on_uplink("imsi-001010000000001",
                             ue_cp(static_cast<std::uint8_t>((ti + 1) % 7), CpMessageType::Ack),
                             std::nullopt));
            CpMessage mo = ue_cp(ti, CpMessageType::Ack);
            mo.ti_flag = false;
            EXPECT_FALSE(mt.on_uplink("imsi-001010000000001", mo, std::nullopt));

            EXPECT_TRUE(
                mt.on_uplink("imsi-001010000000001", ue_cp(ti, CpMessageType::Ack), std::nullopt));
            const RpMessage rp = ue_rp_ack();
            EXPECT_TRUE(mt.on_uplink("imsi-001010000000001",
                                     ue_cp(ti, CpMessageType::Data, encode_rp(rp)),
                                     rp,
                                     R"({"nrLocation":{}})"));
        });
    });
    ue.join();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rp.mti, RpMti::AckMsToNetwork);
    EXPECT_EQ(result->rpdu, encode_rp(ue_rp_ack()));
    EXPECT_EQ(result->ue_location_json, R"({"nrLocation":{}})");
    EXPECT_EQ(mt.in_flight(), 0U);
}

TEST(SmsfMtSms, RpErrorIsADeliveryReportToo) {
    smsf::MtTransactions mt({std::chrono::seconds(5), 4});
    RpMessage err;
    err.mti = RpMti::ErrorMsToNetwork;
    err.message_reference = 0x11;
    err.cause = 22; // Memory capacity exceeded (TS 24.011 table 8.4 part 2)
    auto result = mt.run("imsi-1", [&](std::uint8_t ti) {
        mt.on_uplink("imsi-1", ue_cp(ti, CpMessageType::Data, encode_rp(err)), err);
    });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->rp.mti, RpMti::ErrorMsToNetwork);
    EXPECT_EQ(*result->rp.cause, 22);
}

TEST(SmsfMtSms, FailuresAmfRefusalCpErrorAndTr1n) {
    smsf::MtTransactions mt({std::chrono::milliseconds(200), 4});
    auto refused =
        mt.run("imsi-1", [&](std::uint8_t ti) { mt.downlink_result("imsi-1", ti, false); });
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error(), smsf::MtFailure::NotDelivered);

    auto cp_error = mt.run("imsi-1", [&](std::uint8_t ti) {
        mt.on_uplink("imsi-1", ue_cp(ti, CpMessageType::Error), std::nullopt);
    });
    ASSERT_FALSE(cp_error.has_value());
    EXPECT_EQ(cp_error.error(), smsf::MtFailure::CpError);

    // CP-ACK alone is not a delivery report: TR1N runs out.
    const auto start = std::chrono::steady_clock::now();
    auto timeout = mt.run("imsi-1", [&](std::uint8_t ti) {
        mt.on_uplink("imsi-1", ue_cp(ti, CpMessageType::Ack), std::nullopt);
    });
    ASSERT_FALSE(timeout.has_value());
    EXPECT_EQ(timeout.error(), smsf::MtFailure::Timeout);
    EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(200));
    EXPECT_EQ(mt.in_flight(), 0U);
}

TEST(SmsfMtSms, TiRotatesSoALateMessageForAnOldTransactionIsIgnored) {
    smsf::MtTransactions mt({std::chrono::milliseconds(100), 4});
    std::uint8_t first = 0xFF;
    (void)mt.run("imsi-1", [&](std::uint8_t ti) { first = ti; });
    std::uint8_t second = 0xFF;
    auto r = mt.run("imsi-1", [&](std::uint8_t ti) {
        second = ti;
        EXPECT_FALSE(mt.on_uplink("imsi-1", ue_cp(first, CpMessageType::Error), std::nullopt));
    });
    EXPECT_NE(first, second);
    EXPECT_LE(second, 6); // TIO 7 is the extended TI, never allocated
    EXPECT_EQ(r.error(), smsf::MtFailure::Timeout);
}

TEST(SmsfMtSms, InFlightCapAnswersCongestionAndOneUeIsSerialised) {
    smsf::MtTransactions mt({std::chrono::seconds(5), 1});
    std::atomic<bool> inside{false};
    std::uint8_t ti_a = 0;
    std::thread a([&] {
        (void)mt.run("imsi-A", [&](std::uint8_t ti) {
            ti_a = ti;
            inside = true;
        });
    });
    while (!inside) {
        std::this_thread::yield();
    }
    // The only slot is taken: a second MT SMS (any UE) is refused, not queued behind it.
    auto busy = mt.run("imsi-B", [](std::uint8_t) {});
    ASSERT_FALSE(busy.has_value());
    EXPECT_EQ(busy.error(), smsf::MtFailure::Congestion);
    mt.on_uplink("imsi-A", ue_cp(ti_a, CpMessageType::Error), std::nullopt);
    a.join();

    // TS 24.011 clause 3.2: no parallel transfer in the same direction -- a second MT SMS for the
    // same UE starts only once the first has finished.
    smsf::MtTransactions serial({std::chrono::seconds(5), 4});
    std::atomic<int> order{0};
    std::atomic<bool> first_in{false};
    std::uint8_t ti_first = 0;
    std::thread first([&] {
        (void)serial.run("imsi-C", [&](std::uint8_t ti) {
            ti_first = ti;
            order = 1;
            first_in = true;
        });
    });
    while (!first_in) {
        std::this_thread::yield();
    }
    std::thread second([&] {
        (void)serial.run("imsi-C", [&](std::uint8_t ti) {
            order = order * 10 + 2;
            serial.on_uplink("imsi-C", ue_cp(ti, CpMessageType::Error), std::nullopt);
        });
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(order.load(), 1); // the second is still waiting for the UE's MT slot
    serial.on_uplink("imsi-C", ue_cp(ti_first, CpMessageType::Error), std::nullopt);
    first.join();
    second.join();
    EXPECT_EQ(order.load(), 12);
}
