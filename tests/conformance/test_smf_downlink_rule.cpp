// ADR-0642: the SMF's downlink rule (nfs/smf/src/downlink_rule.cpp), checked
// against TS 29.244 V19.6.0: 8.2.62 (UE IP Address, S/D bit), 8.2.75 (QER ID), 8.2.89 (QFI) and
// Table 7.5.2.5-1 (Create QER: QER ID and Gate Status mandatory).

#include <array>
#include <cstdint>
#include <vector>

#include "downlink_rule.hpp"
#include "pfcp_core/ie.hpp"
#include "pfcp_core/session_ies.hpp"

#include <gtest/gtest.h>

namespace {

const pfcp_core::Ie* child(const std::vector<pfcp_core::Ie>& ies, pfcp_core::IeType type) {
    return pfcp_core::find_ie(ies, static_cast<std::uint16_t>(type));
}

std::vector<pfcp_core::Ie> decode_group(const pfcp_core::Ie* ie) {
    if (ie == nullptr) {
        return {};
    }
    return pfcp_core::decode_ies(ie->value).value_or(std::vector<pfcp_core::Ie>{});
}

constexpr std::array<std::uint8_t, 4> kGnb{127, 0, 0, 9};
constexpr std::array<std::uint8_t, 4> kUe{10, 45, 0, 7};

} // namespace

TEST(SmfDownlinkRule, PdrMatchesTheUeAsDestinationAndReferencesTheSessionQer) {
    const auto ies = pfcp_core::decode_ies(
        smf::build_downlink_rule_ies(0x11223344, kGnb, kUe, 9, 1, /*create_qer=*/false));
    ASSERT_TRUE(ies.has_value());

    const auto pdr = decode_group(child(*ies, pfcp_core::IeType::CreatePdr));
    ASSERT_FALSE(pdr.empty());
    const auto* qer_id = child(pdr, pfcp_core::IeType::QerId);
    ASSERT_NE(qer_id, nullptr) << "the downlink PDR references no QER";
    EXPECT_EQ(pfcp_core::decode_qer_id(qer_id->value), std::optional<std::uint32_t>{1});

    const auto pdi = decode_group(child(pdr, pfcp_core::IeType::Pdi));
    const auto* source = child(pdi, pfcp_core::IeType::SourceInterface);
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(pfcp_core::decode_interface_value(source->value),
              std::optional<pfcp_core::InterfaceValue>{pfcp_core::InterfaceValue::Core});
    const auto* ue_ip = child(pdi, pfcp_core::IeType::UeIpAddress);
    ASSERT_NE(ue_ip, nullptr) << "the downlink PDI does not match the UE IP address";
    // Octet 5: S/D (bit 3) = 1, destination; V4 (bit 2) = 1. Then the IPv4 address.
    EXPECT_EQ(ue_ip->value, (std::vector<std::uint8_t>{0x06, 10, 45, 0, 7}));

    const auto far = decode_group(child(*ies, pfcp_core::IeType::CreateFar));
    const auto fwd = decode_group(child(far, pfcp_core::IeType::ForwardingParameters));
    const auto* ohc = child(fwd, pfcp_core::IeType::OuterHeaderCreation);
    ASSERT_NE(ohc, nullptr);
    const auto decoded_ohc = pfcp_core::decode_outer_header_creation_gtpu_ipv4(ohc->value);
    ASSERT_TRUE(decoded_ohc.has_value());
    EXPECT_EQ(decoded_ohc->teid, 0x11223344u);
    EXPECT_EQ(decoded_ohc->ipv4, kGnb);

    // The session QER already exists, so this message must not create a second one: the UPF holds
    // one QER per TEID and would replace the Session-AMBR one.
    EXPECT_EQ(child(*ies, pfcp_core::IeType::CreateQer), nullptr);
}

TEST(SmfDownlinkRule, CreatesTheSessionQerWithTheQfiWhenEstablishmentDidNot) {
    const auto ies = pfcp_core::decode_ies(
        smf::build_downlink_rule_ies(0x11223344, kGnb, kUe, 9, 1, /*create_qer=*/true));
    ASSERT_TRUE(ies.has_value());
    const auto qer = decode_group(child(*ies, pfcp_core::IeType::CreateQer));
    ASSERT_FALSE(qer.empty());
    const auto* qer_id = child(qer, pfcp_core::IeType::QerId);
    const auto* gate = child(qer, pfcp_core::IeType::GateStatus);
    const auto* qfi = child(qer, pfcp_core::IeType::Qfi);
    ASSERT_NE(qer_id, nullptr);
    ASSERT_NE(gate, nullptr) << "Gate Status is mandatory in Create QER (Table 7.5.2.5-1)";
    ASSERT_NE(qfi, nullptr);
    EXPECT_EQ(pfcp_core::decode_qer_id(qer_id->value), std::optional<std::uint32_t>{1});
    const auto decoded_gate = pfcp_core::decode_gate_status(gate->value);
    ASSERT_TRUE(decoded_gate.has_value());
    EXPECT_FALSE(decoded_gate->ul_closed);
    EXPECT_FALSE(decoded_gate->dl_closed);
    EXPECT_EQ(pfcp_core::decode_qfi(qfi->value), std::optional<std::uint8_t>{9});
    EXPECT_EQ(child(qer, pfcp_core::IeType::Mbr), nullptr);
}

TEST(SmfDownlinkRule, WithoutAUeAddressThePdiMatchesSourceInterfaceOnly) {
    const auto ies = pfcp_core::decode_ies(
        smf::build_downlink_rule_ies(0x11223344, kGnb, std::nullopt, 1, 1, false));
    ASSERT_TRUE(ies.has_value());
    const auto pdr = decode_group(child(*ies, pfcp_core::IeType::CreatePdr));
    const auto pdi = decode_group(child(pdr, pfcp_core::IeType::Pdi));
    EXPECT_NE(child(pdi, pfcp_core::IeType::SourceInterface), nullptr);
    EXPECT_EQ(child(pdi, pfcp_core::IeType::UeIpAddress), nullptr);
}
