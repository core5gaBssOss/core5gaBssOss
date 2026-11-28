#include "n2_sm_info.hpp"

#include <cstdlib>
#include <cstring>

#include "ngap_core/ngap_codec.hpp"

extern "C" {
#include <GTPTunnel.h>
#include <NonDynamic5QIDescriptor.h>
#include <PDUSessionAggregateMaximumBitRate.h>
#include <PDUSessionResourceSetupRequestTransfer.h>
#include <PDUSessionType.h>
#include <QosFlowSetupRequestItem.h>
#include <QosFlowSetupRequestList.h>
#include <UPTransportLayerInformation.h>
}

namespace smf::n2 {

namespace {

// ProtocolIE-IDs, TS 38.413 9.4.7 (specs/NGAP/ngap-17.9.asn).
constexpr long kIdPduSessionAggregateMaximumBitRate = 130;
constexpr long kIdPduSessionType = 134;
constexpr long kIdQosFlowSetupRequestList = 136;
constexpr long kIdUlNguUpTnlInformation = 139;

} // namespace

std::vector<std::uint8_t> encode_setup_request_transfer(const SetupRequestTransferInput& in) {
    PDUSessionResourceSetupRequestTransfer_t transfer{};

    // 9.3.4.1: "PDU Session Aggregate Maximum Bit Rate ... shall be present when at least one
    // Non-GBR QoS flow is being setup and is ignored otherwise." The one flow set up here is the
    // default QoS flow; it is included whenever the SMF has a Session-AMBR for it.
    if (in.ambr_dl_bps.has_value() && in.ambr_ul_bps.has_value()) {
        PDUSessionAggregateMaximumBitRate_t ambr{};
        if (asn_ulong2INTEGER(&ambr.pDUSessionAggregateMaximumBitRateDL,
                              static_cast<unsigned long>(*in.ambr_dl_bps)) != 0 ||
            asn_ulong2INTEGER(&ambr.pDUSessionAggregateMaximumBitRateUL,
                              static_cast<unsigned long>(*in.ambr_ul_bps)) != 0) {
            ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_PDUSessionAggregateMaximumBitRate, &ambr);
            ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_PDUSessionResourceSetupRequestTransfer,
                                          &transfer);
            return {};
        }
        ::ngap::add_ie(transfer.protocolIEs,
                       ::ngap::make_ie(kIdPduSessionAggregateMaximumBitRate,
                                       Criticality_reject,
                                       &asn_DEF_PDUSessionAggregateMaximumBitRate,
                                       &ambr));
        ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_PDUSessionAggregateMaximumBitRate, &ambr);
    }

    UPTransportLayerInformation_t ul_info{};
    ul_info.present = UPTransportLayerInformation_PR_gTPTunnel;
    auto* gtp_tunnel = static_cast<GTPTunnel_t*>(std::calloc(1, sizeof(GTPTunnel_t)));
    const std::uint8_t teid_bytes[4] = {static_cast<std::uint8_t>((in.ul_teid >> 24) & 0xFF),
                                        static_cast<std::uint8_t>((in.ul_teid >> 16) & 0xFF),
                                        static_cast<std::uint8_t>((in.ul_teid >> 8) & 0xFF),
                                        static_cast<std::uint8_t>(in.ul_teid & 0xFF)};
    gtp_tunnel->gTP_TEID.buf = static_cast<std::uint8_t*>(std::malloc(4));
    std::memcpy(gtp_tunnel->gTP_TEID.buf, teid_bytes, 4);
    gtp_tunnel->gTP_TEID.size = 4;
    gtp_tunnel->transportLayerAddress.buf = static_cast<std::uint8_t*>(std::malloc(4));
    std::memcpy(gtp_tunnel->transportLayerAddress.buf, in.ul_ipv4.data(), 4);
    gtp_tunnel->transportLayerAddress.size = 4;
    gtp_tunnel->transportLayerAddress.bits_unused = 0;
    ul_info.choice.gTPTunnel = gtp_tunnel;
    ::ngap::add_ie(transfer.protocolIEs,
                   ::ngap::make_ie(kIdUlNguUpTnlInformation,
                                   Criticality_reject,
                                   &asn_DEF_UPTransportLayerInformation,
                                   &ul_info));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_UPTransportLayerInformation, &ul_info);

    PDUSessionType_t pdu_type = PDUSessionType_ipv4;
    ::ngap::add_ie(
        transfer.protocolIEs,
        ::ngap::make_ie(kIdPduSessionType, Criticality_reject, &asn_DEF_PDUSessionType, &pdu_type));

    QosFlowSetupRequestList_t qos_list{};
    auto* qos_item =
        static_cast<QosFlowSetupRequestItem_t*>(std::calloc(1, sizeof(QosFlowSetupRequestItem_t)));
    qos_item->qosFlowIdentifier = in.flow.qfi & 0x3F;
    qos_item->qosFlowLevelQosParameters.qosCharacteristics.present =
        QosCharacteristics_PR_nonDynamic5QI;
    auto* non_dynamic =
        static_cast<NonDynamic5QIDescriptor_t*>(std::calloc(1, sizeof(NonDynamic5QIDescriptor_t)));
    non_dynamic->fiveQI = in.flow.five_qi;
    qos_item->qosFlowLevelQosParameters.qosCharacteristics.choice.nonDynamic5QI = non_dynamic;
    auto& arp = qos_item->qosFlowLevelQosParameters.allocationAndRetentionPriority;
    arp.priorityLevelARP = in.flow.arp_priority_level;
    arp.pre_emptionCapability = in.flow.arp_may_trigger_pre_emption
                                    ? Pre_emptionCapability_may_trigger_pre_emption
                                    : Pre_emptionCapability_shall_not_trigger_pre_emption;
    arp.pre_emptionVulnerability = in.flow.arp_pre_emptable
                                       ? Pre_emptionVulnerability_pre_emptable
                                       : Pre_emptionVulnerability_not_pre_emptable;
    ASN_SEQUENCE_ADD(&qos_list.list, qos_item);
    ::ngap::add_ie(transfer.protocolIEs,
                   ::ngap::make_ie(kIdQosFlowSetupRequestList,
                                   Criticality_reject,
                                   &asn_DEF_QosFlowSetupRequestList,
                                   &qos_list));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_QosFlowSetupRequestList, &qos_list);

    auto bytes = ::ngap::encode_value(&asn_DEF_PDUSessionResourceSetupRequestTransfer, &transfer);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_PDUSessionResourceSetupRequestTransfer, &transfer);
    return bytes;
}

} // namespace smf::n2
