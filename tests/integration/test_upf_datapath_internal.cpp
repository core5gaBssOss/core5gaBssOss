// ADR-0484 (LI stage 4): unit-tests the pure logic pulled out of datapath.cpp's ring-buffer
// handling (nfs/upf/src/datapath_internal.hpp) -- TUN write + CC-POI dispatch for one
// decapsulated T-PDU ring buffer record -- without a real BPF ring buffer, CAP_NET_ADMIN, or
// CAP_BPF. Pure logic, no ports, same class as test_nrf_discovery_authz.cpp (ADR-0477).

#include <algorithm>
#include <cstring>
#include <vector>

#include "datapath_internal.hpp"

#include <gtest/gtest.h>

namespace {

upf::detail::TpduRecord make_record(std::uint32_t teid, const std::vector<std::uint8_t>& payload) {
    upf::detail::TpduRecord rec{};
    rec.length = static_cast<std::uint16_t>(payload.size());
    rec.teid = teid;
    std::memcpy(rec.data, payload.data(), payload.size());
    return rec;
}

std::vector<std::uint8_t> make_payload(std::size_t n, std::uint8_t fill = 0x42) {
    return std::vector<std::uint8_t>(n, fill);
}

} // namespace

TEST(UpfDatapathInternal, ReconstructedGtpuHeaderMatchesTs29281MinimalHeader) {
    const auto hdr = upf::detail::reconstruct_gtpu_header(0x12345678, 9);
    // TS 29.281 Figure 5.1-1: version=1, PT=1, spare/E/S/PN=0 -> 0b001'1'0'0'0'0 = 0x30.
    EXPECT_EQ(hdr[0], 0x30);
    EXPECT_EQ(hdr[1], 255); // G-PDU, Table 6.1-1
    EXPECT_EQ(hdr[2], 0x00);
    EXPECT_EQ(hdr[3], 0x09); // length, big-endian
    EXPECT_EQ(hdr[4], 0x12);
    EXPECT_EQ(hdr[5], 0x34);
    EXPECT_EQ(hdr[6], 0x56);
    EXPECT_EQ(hdr[7], 0x78); // TEID, big-endian
}

TEST(UpfDatapathInternal, ProcessTpduRecordDispatchesFullGtpuPacketToPacketHandler) {
    constexpr std::uint32_t kTeid = 0xAABBCCDD;
    const auto payload = make_payload(16, 0x7A);
    const auto rec = make_record(kTeid, payload);

    std::uint32_t seen_teid = 0;
    std::vector<std::uint8_t> seen_packet;
    const upf::Datapath::PacketHandler handler = [&](std::uint32_t teid,
                                                     std::span<const std::uint8_t> gtpu_packet) {
        seen_teid = teid;
        seen_packet.assign(gtpu_packet.begin(), gtpu_packet.end());
    };

    // tun_fd=-1: the TUN write fails and is logged (existing disclosed non-fatal path) -- dispatch
    // to the CC-POI must still happen regardless, same as a real tap would.
    upf::detail::process_tpdu_record(rec, sizeof(rec), /*tun_fd=*/-1, handler);

    ASSERT_EQ(seen_teid, kTeid);
    ASSERT_EQ(seen_packet.size(), 8 + payload.size());
    const auto expected_header = upf::detail::reconstruct_gtpu_header(kTeid, rec.length);
    EXPECT_TRUE(std::equal(expected_header.begin(), expected_header.end(), seen_packet.begin()));
    EXPECT_TRUE(std::equal(payload.begin(), payload.end(), seen_packet.begin() + 8));
}

TEST(UpfDatapathInternal, ProcessTpduRecordNoHandlerIsANoOp) {
    const auto rec = make_record(0x1, make_payload(4));
    // No PacketHandler set (default-constructed std::function) -- must not crash or throw.
    upf::detail::process_tpdu_record(
        rec, sizeof(rec), /*tun_fd=*/-1, upf::Datapath::PacketHandler{});
}

TEST(UpfDatapathInternal, ProcessTpduRecordTooSmallSampleDoesNotInvokeHandler) {
    const auto rec = make_record(0x1, make_payload(4));
    bool invoked = false;
    const upf::Datapath::PacketHandler handler = [&](std::uint32_t, std::span<const std::uint8_t>) {
        invoked = true;
    };

    // data_sz smaller than sizeof(TpduRecord): the real ring buffer trampoline already guards
    // against this before calling in, but process_tpdu_record re-checks defensively.
    upf::detail::process_tpdu_record(rec, sizeof(rec) - 1, /*tun_fd=*/-1, handler);

    EXPECT_FALSE(invoked);
}

TEST(UpfDatapathInternal, ProcessTpduRecordImplausibleLengthDoesNotInvokeHandler) {
    upf::detail::TpduRecord rec{};
    rec.teid = 0x1;
    rec.length = sizeof(rec.data) + 1; // claims more than the record can actually hold
    bool invoked = false;
    const upf::Datapath::PacketHandler handler = [&](std::uint32_t, std::span<const std::uint8_t>) {
        invoked = true;
    };

    upf::detail::process_tpdu_record(rec, sizeof(rec), /*tun_fd=*/-1, handler);

    EXPECT_FALSE(invoked);
}
