#include "downlink_rule.hpp"

#include "pfcp_core/ie.hpp"
#include "pfcp_core/session_ies.hpp"

namespace smf {

std::vector<std::uint8_t>
build_downlink_rule_ies(std::uint32_t gnb_teid,
                        const std::array<std::uint8_t, 4>& gnb_ipv4,
                        std::optional<std::array<std::uint8_t, 4>> ue_ipv4,
                        std::uint8_t qfi,
                        std::uint32_t qer_id,
                        bool create_qer) {
    std::vector<std::uint8_t> dl_pdi;
    pfcp_core::encode_ie(dl_pdi,
                         static_cast<std::uint16_t>(pfcp_core::IeType::SourceInterface),
                         pfcp_core::encode_source_interface(pfcp_core::InterfaceValue::Core));
    if (ue_ipv4.has_value()) {
        pfcp_core::encode_ie(dl_pdi,
                             static_cast<std::uint16_t>(pfcp_core::IeType::UeIpAddress),
                             pfcp_core::encode_ue_ip_address_ipv4(*ue_ipv4, /*destination=*/true));
    }

    std::vector<std::uint8_t> dl_create_pdr;
    pfcp_core::encode_ie(dl_create_pdr,
                         static_cast<std::uint16_t>(pfcp_core::IeType::PdrId),
                         pfcp_core::encode_pdr_id(kDlPdrId));
    pfcp_core::encode_ie(dl_create_pdr,
                         static_cast<std::uint16_t>(pfcp_core::IeType::Precedence),
                         pfcp_core::encode_precedence(100));
    pfcp_core::encode_ie(dl_create_pdr, static_cast<std::uint16_t>(pfcp_core::IeType::Pdi), dl_pdi);
    pfcp_core::encode_ie(dl_create_pdr,
                         static_cast<std::uint16_t>(pfcp_core::IeType::FarId),
                         pfcp_core::encode_far_id(kDlFarId));
    pfcp_core::encode_ie(dl_create_pdr,
                         static_cast<std::uint16_t>(pfcp_core::IeType::QerId),
                         pfcp_core::encode_qer_id(qer_id));

    std::vector<std::uint8_t> dl_forwarding_parameters;
    pfcp_core::encode_ie(
        dl_forwarding_parameters,
        static_cast<std::uint16_t>(pfcp_core::IeType::DestinationInterface),
        pfcp_core::encode_destination_interface(pfcp_core::InterfaceValue::Access));
    pfcp_core::encode_ie(dl_forwarding_parameters,
                         static_cast<std::uint16_t>(pfcp_core::IeType::OuterHeaderCreation),
                         pfcp_core::encode_outer_header_creation_gtpu_ipv4(gnb_teid, gnb_ipv4));

    std::vector<std::uint8_t> dl_create_far;
    pfcp_core::encode_ie(dl_create_far,
                         static_cast<std::uint16_t>(pfcp_core::IeType::FarId),
                         pfcp_core::encode_far_id(kDlFarId));
    pfcp_core::encode_ie(dl_create_far,
                         static_cast<std::uint16_t>(pfcp_core::IeType::ApplyAction),
                         pfcp_core::encode_apply_action_forward());
    pfcp_core::encode_ie(dl_create_far,
                         static_cast<std::uint16_t>(pfcp_core::IeType::ForwardingParameters),
                         dl_forwarding_parameters);

    // TS 29.244 Table 7.5.2.5-1: QER ID and Gate Status are mandatory. The gate is open both ways:
    // this QER marks packets with the QFI, it does not block them.
    std::vector<std::uint8_t> dl_create_qer;
    pfcp_core::encode_ie(dl_create_qer,
                         static_cast<std::uint16_t>(pfcp_core::IeType::QerId),
                         pfcp_core::encode_qer_id(qer_id));
    pfcp_core::encode_ie(dl_create_qer,
                         static_cast<std::uint16_t>(pfcp_core::IeType::GateStatus),
                         pfcp_core::encode_gate_status(pfcp_core::GateStatus{}));
    pfcp_core::encode_ie(dl_create_qer,
                         static_cast<std::uint16_t>(pfcp_core::IeType::Qfi),
                         pfcp_core::encode_qfi(qfi));

    std::vector<std::uint8_t> mod_ies;
    pfcp_core::encode_ie(
        mod_ies, static_cast<std::uint16_t>(pfcp_core::IeType::CreatePdr), dl_create_pdr);
    pfcp_core::encode_ie(
        mod_ies, static_cast<std::uint16_t>(pfcp_core::IeType::CreateFar), dl_create_far);
    if (create_qer) {
        pfcp_core::encode_ie(
            mod_ies, static_cast<std::uint16_t>(pfcp_core::IeType::CreateQer), dl_create_qer);
    }
    return mod_ies;
}

} // namespace smf
