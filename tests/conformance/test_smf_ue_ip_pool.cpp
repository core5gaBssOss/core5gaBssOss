// ADR-0525: SMF UE IPv4 allocation (TS 23.501 5.8.2.2.1) -- the pool, the PDU address IE in the PDU
// SESSION ESTABLISHMENT ACCEPT (TS 24.501 9.11.4.10) and the PFCP UE IP Address IE (TS 29.244
// 8.2.62).

#include <stdexcept>
#include <vector>

#include "nas_5gsm_codec.hpp"
#include "pfcp_core/session_ies.hpp"
#include "ue_ip_pool.hpp"

#include <gtest/gtest.h>

namespace {

using Addr = std::array<std::uint8_t, 4>;

TEST(SmfUeIpPool, HandsOutTheLowestFreeHostAndReusesAReleasedOne) {
    smf::UeIpPool pool({{"internet", "10.45.0.0/30"}}); // hosts .1 and .2
    EXPECT_EQ(pool.allocate("internet"), (Addr{10, 45, 0, 1}));
    EXPECT_EQ(pool.allocate("internet"), (Addr{10, 45, 0, 2}));
    EXPECT_FALSE(pool.allocate("internet").has_value()) << "network/broadcast never handed out";
    pool.release("internet", Addr{10, 45, 0, 1});
    EXPECT_EQ(pool.allocate("internet"), (Addr{10, 45, 0, 1}));
}

TEST(SmfUeIpPool, AnUnknownDnnHasNoAddressAndABadPoolIsAConfigError) {
    smf::UeIpPool pool({{"internet", "10.45.0.0/16"}});
    EXPECT_FALSE(pool.allocate("ims").has_value());
    EXPECT_THROW(smf::UeIpPool({{"internet", "10.45.0.0"}}), std::invalid_argument);
    EXPECT_THROW(smf::UeIpPool({{"internet", "10.45.0.0/31"}}), std::invalid_argument);
    EXPECT_EQ(smf::UeIpPool::parse("10.45.0.7"), (Addr{10, 45, 0, 7}));
    EXPECT_FALSE(smf::UeIpPool::parse("10.45.0").has_value());
}

TEST(SmfUeIpPool, AcceptCarriesThePduAddressAfterTheMandatoryIes) {
    const auto without = smf::nas5gsm::encode_establishment_accept(5, 1, "100 Mbps", "100 Mbps", 9);
    const auto with = smf::nas5gsm::encode_establishment_accept(
        5, 1, "100 Mbps", "100 Mbps", 9, Addr{10, 45, 0, 1});
    ASSERT_EQ(with.size(), without.size() + 7);
    // IEI 0x29, length 5, PDU session type IPv4 (001), then the address.
    EXPECT_EQ(std::vector<std::uint8_t>(with.end() - 7, with.end()),
              (std::vector<std::uint8_t>{0x29, 0x05, 0x01, 10, 45, 0, 1}));
}

TEST(SmfUeIpPool, PfcpUeIpAddressIsV4WithTheSdFlag) {
    EXPECT_EQ(pfcp_core::encode_ue_ip_address_ipv4(Addr{10, 45, 0, 1}, false),
              (std::vector<std::uint8_t>{0x02, 10, 45, 0, 1})); // V4, S/D=0 source
    EXPECT_EQ(pfcp_core::encode_ue_ip_address_ipv4(Addr{10, 45, 0, 1}, true),
              (std::vector<std::uint8_t>{0x06, 10, 45, 0, 1})); // V4, S/D=1 destination
}

} // namespace
