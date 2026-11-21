#pragma once

// ADR-0605 (ADR-0570 class, AMF part): every URI the AMF builds from a subscriber-derived value.
// The SUPI is not ours to trust as a URI segment: TS 29.571 lets a SUPI carry any character, and
// the AMF's own NAS decoder (nas_codec.cpp bcd_digit) turns an MSIN nibble 0xA..0xE into '?'. So
// each such value is percent-encoded as one path segment (sbi_core::uri::encode_path_segment)
// where the URI is built; the peer's sbi_core server decodes it back after splitting the path
// (ADR-0549). Kept header-only so tests/integration can call the exact builders the AMF uses.
//
// Not here, on purpose: SM context refs and the PCF policy association id are the last segment
// of the peer's own Location header (already URI form; encoding them again would double-encode a
// '%'), and the AUSF's 5g-aka href is a link the AUSF chose. Both are used as given.

#include "sbi_core/uri.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace amf::peer_uris {

// Nsmsf_SMService_Activate / UpdateSMSContext: PUT {apiRoot}/nsmsf-sms/v2/ue-contexts/{supi}
// (TS29540_Nsmsf_SMService.yaml path `/ue-contexts/{supi}`).
inline std::string smsf_ue_context(std::string_view smsf_base, std::string_view supi) {
    return std::string(smsf_base) + "/nsmsf-sms/v2/ue-contexts/" +
           sbi_core::uri::encode_path_segment(supi);
}

// Nsmsf_SMService_UplinkSMS: POST {apiRoot}/nsmsf-sms/v2/ue-contexts/{supi}/sendsms
// (TS29540_Nsmsf_SMService.yaml path `/ue-contexts/{supi}/sendsms`).
inline std::string smsf_send_sms(std::string_view smsf_base, std::string_view supi) {
    return smsf_ue_context(smsf_base, supi) + "/sendsms";
}

// The smContextStatusUri the AMF gives the SMF (ADR-0523): the AMF's own callback route
// `/namf-callback/v1/sm-context-status/{supi}/{pduSessionId}` in main.cpp.
inline std::string sm_context_status_callback(std::string_view self_base,
                                              std::string_view supi,
                                              std::uint32_t pdu_session_id) {
    return std::string(self_base) + "/namf-callback/v1/sm-context-status/" +
           sbi_core::uri::encode_path_segment(supi) + "/" + std::to_string(pdu_session_id);
}

} // namespace amf::peer_uris
