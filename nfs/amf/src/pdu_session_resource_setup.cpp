#include "pdu_session_resource_setup.hpp"

#include <cstdlib>
#include <cstring>

#include "ngap_core/ngap_codec.hpp"

extern "C" {
#include <AMF-UE-NGAP-ID.h>
#include <InitiatingMessage.h>
#include <NGAP-PDU.h>
#include <PDUSessionResourceFailedToSetupItemSURes.h>
#include <PDUSessionResourceFailedToSetupListSURes.h>
#include <PDUSessionResourceSetupItemSUReq.h>
#include <PDUSessionResourceSetupItemSURes.h>
#include <PDUSessionResourceSetupListSUReq.h>
#include <PDUSessionResourceSetupListSURes.h>
#include <PDUSessionResourceSetupRequest.h>
#include <PDUSessionResourceSetupResponse.h>
#include <RAN-UE-NGAP-ID.h>
#include <SuccessfulOutcome.h>
}

namespace amf::ngap {

namespace {

// specs/NGAP/ngap-17.9.asn: id-PDUSessionResourceSetup ProcedureCode ::= 29; ProtocolIE-IDs
// id-AMF-UE-NGAP-ID 10, id-RAN-UE-NGAP-ID 85, id-PDUSessionResourceSetupListSUReq 74,
// id-PDUSessionResourceSetupListSURes 75, id-PDUSessionResourceFailedToSetupListSURes 58.
constexpr long kProcedurePduSessionResourceSetup = 29;
constexpr long kIdAmfUeNgapId = 10;
constexpr long kIdRanUeNgapId = 85;
constexpr long kIdSetupListSuReq = 74;
constexpr long kIdSetupListSuRes = 75;
constexpr long kIdFailedToSetupListSuRes = 58;

void set_octets(OCTET_STRING_t& out, const std::uint8_t* data, std::size_t len) {
    out.buf = static_cast<std::uint8_t*>(std::malloc(len == 0 ? 1 : len));
    if (len != 0) {
        std::memcpy(out.buf, data, len);
    }
    out.size = len;
}

std::vector<std::uint8_t> octets(const OCTET_STRING_t& s) {
    if (s.buf == nullptr) {
        return {};
    }
    return {s.buf, s.buf + s.size};
}

int hex_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

std::optional<std::array<std::uint8_t, 3>> parse_sd(const std::string& sd) {
    if (sd.size() != 6) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 3> out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const int hi = hex_nibble(sd[2 * i]);
        const int lo = hex_nibble(sd[2 * i + 1]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return out;
}

std::vector<std::uint8_t> encode_setup_request(const SetupRequestInput& in) {
    PDUSessionResourceSetupRequest_t req{};

    AMF_UE_NGAP_ID_t amf_ue_id{};
    asn_ulong2INTEGER(&amf_ue_id, in.amf_ue_id);
    ::ngap::add_ie(
        req.protocolIEs,
        ::ngap::make_ie(kIdAmfUeNgapId, Criticality_reject, &asn_DEF_AMF_UE_NGAP_ID, &amf_ue_id));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_AMF_UE_NGAP_ID, &amf_ue_id);

    RAN_UE_NGAP_ID_t ran_ue_id = in.ran_ue_id;
    ::ngap::add_ie(
        req.protocolIEs,
        ::ngap::make_ie(kIdRanUeNgapId, Criticality_reject, &asn_DEF_RAN_UE_NGAP_ID, &ran_ue_id));

    PDUSessionResourceSetupListSUReq_t list{};
    auto* item = static_cast<PDUSessionResourceSetupItemSUReq_t*>(
        std::calloc(1, sizeof(PDUSessionResourceSetupItemSUReq_t)));
    item->pDUSessionID = in.pdu_session_id;
    if (!in.pdu_session_nas_pdu.empty()) {
        item->pDUSessionNAS_PDU = static_cast<NAS_PDU_t*>(std::calloc(1, sizeof(NAS_PDU_t)));
        set_octets(
            *item->pDUSessionNAS_PDU, in.pdu_session_nas_pdu.data(), in.pdu_session_nas_pdu.size());
    }
    set_octets(item->s_NSSAI.sST, &in.s_nssai.sst, 1);
    if (in.s_nssai.sd.has_value()) {
        item->s_NSSAI.sD = static_cast<SD_t*>(std::calloc(1, sizeof(SD_t)));
        set_octets(*item->s_NSSAI.sD, in.s_nssai.sd->data(), 3);
    }
    set_octets(item->pDUSessionResourceSetupRequestTransfer,
               in.setup_request_transfer.data(),
               in.setup_request_transfer.size());
    ASN_SEQUENCE_ADD(&list.list, item);
    ::ngap::add_ie(req.protocolIEs,
                   ::ngap::make_ie(kIdSetupListSuReq,
                                   Criticality_reject,
                                   &asn_DEF_PDUSessionResourceSetupListSUReq,
                                   &list));
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_PDUSessionResourceSetupListSUReq, &list);

    NGAP_PDU_t pdu{};
    pdu.present = NGAP_PDU_PR_initiatingMessage;
    pdu.choice.initiatingMessage =
        static_cast<InitiatingMessage_t*>(std::calloc(1, sizeof(InitiatingMessage_t)));
    pdu.choice.initiatingMessage->procedureCode = kProcedurePduSessionResourceSetup;
    // 9.4.3 Elementary Procedure Definitions: pDUSessionResourceSetup is CRITICALITY reject.
    pdu.choice.initiatingMessage->criticality = Criticality_reject;
    pdu.choice.initiatingMessage->value.present =
        InitiatingMessage__value_PR_PDUSessionResourceSetupRequest;
    pdu.choice.initiatingMessage->value.choice.PDUSessionResourceSetupRequest = req;
    auto bytes = ::ngap::encode_pdu(pdu);
    ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_NGAP_PDU, &pdu);
    return bytes;
}

std::optional<SetupResponse> decode_setup_response(const std::vector<std::uint8_t>& pdu_bytes) {
    NGAP_PDU_t* pdu = ::ngap::decode_pdu(pdu_bytes);
    if (pdu == nullptr) {
        return std::nullopt;
    }
    if (pdu->present != NGAP_PDU_PR_successfulOutcome ||
        pdu->choice.successfulOutcome->procedureCode != kProcedurePduSessionResourceSetup ||
        pdu->choice.successfulOutcome->value.present !=
            SuccessfulOutcome__value_PR_PDUSessionResourceSetupResponse) {
        ASN_STRUCT_FREE(asn_DEF_NGAP_PDU, pdu);
        return std::nullopt;
    }
    const auto& ies =
        pdu->choice.successfulOutcome->value.choice.PDUSessionResourceSetupResponse.protocolIEs;
    SetupResponse out;

    if (const auto* ie = ::ngap::find_ie(ies, kIdAmfUeNgapId); ie != nullptr) {
        auto* v =
            static_cast<AMF_UE_NGAP_ID_t*>(::ngap::decode_ie_value(&asn_DEF_AMF_UE_NGAP_ID, *ie));
        if (v != nullptr) {
            long value = -1;
            if (asn_INTEGER2long(v, &value) == 0) {
                out.amf_ue_id = value;
            }
            ASN_STRUCT_FREE(asn_DEF_AMF_UE_NGAP_ID, v);
        }
    }
    if (const auto* ie = ::ngap::find_ie(ies, kIdRanUeNgapId); ie != nullptr) {
        auto* v =
            static_cast<RAN_UE_NGAP_ID_t*>(::ngap::decode_ie_value(&asn_DEF_RAN_UE_NGAP_ID, *ie));
        if (v != nullptr) {
            out.ran_ue_id = static_cast<long>(*v);
            ASN_STRUCT_FREE(asn_DEF_RAN_UE_NGAP_ID, v);
        }
    }
    if (const auto* ie = ::ngap::find_ie(ies, kIdSetupListSuRes); ie != nullptr) {
        auto* list = static_cast<PDUSessionResourceSetupListSURes_t*>(
            ::ngap::decode_ie_value(&asn_DEF_PDUSessionResourceSetupListSURes, *ie));
        if (list != nullptr) {
            for (int i = 0; i < list->list.count; ++i) {
                const auto* item = list->list.array[i];
                if (item != nullptr) {
                    out.setup.push_back({item->pDUSessionID,
                                         octets(item->pDUSessionResourceSetupResponseTransfer)});
                }
            }
            ASN_STRUCT_FREE(asn_DEF_PDUSessionResourceSetupListSURes, list);
        }
    }
    if (const auto* ie = ::ngap::find_ie(ies, kIdFailedToSetupListSuRes); ie != nullptr) {
        auto* list = static_cast<PDUSessionResourceFailedToSetupListSURes_t*>(
            ::ngap::decode_ie_value(&asn_DEF_PDUSessionResourceFailedToSetupListSURes, *ie));
        if (list != nullptr) {
            for (int i = 0; i < list->list.count; ++i) {
                const auto* item = list->list.array[i];
                if (item != nullptr) {
                    out.failed.push_back(
                        {item->pDUSessionID,
                         octets(item->pDUSessionResourceSetupUnsuccessfulTransfer)});
                }
            }
            ASN_STRUCT_FREE(asn_DEF_PDUSessionResourceFailedToSetupListSURes, list);
        }
    }
    ASN_STRUCT_FREE(asn_DEF_NGAP_PDU, pdu);
    if (out.amf_ue_id < 0 || out.ran_ue_id < 0) {
        return std::nullopt;
    }
    return out;
}

} // namespace amf::ngap
