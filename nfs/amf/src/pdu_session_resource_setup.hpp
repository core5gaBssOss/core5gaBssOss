#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// ADR-0643: NGAP PDU Session Resource Setup (TS 38.413 V19.4.0 8.2.1, procedure code 29), the N2
// PDU Session Request / Response of TS 23.502 V19.9.0 4.3.2.2.1 steps 12 and 14. The AMF relays
// the SMF's transfers byte for byte in both directions (TS 38.413 9.3.4.1: "This IE is transparent
// to the AMF"); it builds only the message around them.
namespace amf::ngap {

struct SNssaiBytes {
    std::uint8_t sst = 0;
    std::optional<std::array<std::uint8_t, 3>> sd;
};

// Parses an SBI Snssai's `sd` (TS 29.571: 6 hexadecimal characters) into 3 octets.
std::optional<std::array<std::uint8_t, 3>> parse_sd(const std::string& sd);

struct SetupRequestInput {
    std::uint32_t amf_ue_id = 0;
    std::uint32_t ran_ue_id = 0;
    std::uint8_t pdu_session_id = 0;
    SNssaiBytes s_nssai;
    // PDU Session NAS-PDU (NAS-PDU, 9.3.3.4) of the list item (9.2.1.1): the security-protected DL
    // NAS TRANSPORT carrying the N1 SM container. The gNB passes it to the UE only if the session
    // is set up (8.2.1.2).
    std::vector<std::uint8_t> pdu_session_nas_pdu;
    // The SMF's PDU Session Resource Setup Request Transfer (9.3.4.1), unchanged.
    std::vector<std::uint8_t> setup_request_transfer;
};

// Aligned-PER bytes of the whole NGAP-PDU, or empty on an encoding failure. One list item: this
// AMF sets up one PDU session per N1N2MessageTransfer.
std::vector<std::uint8_t> encode_setup_request(const SetupRequestInput& in);

struct SetupResponseItem {
    long pdu_session_id = 0;
    // PDU Session Resource Setup Response Transfer (9.3.4.2) when set up, PDU Session Resource
    // Setup Unsuccessful Transfer (9.3.4.16) when it failed; opaque to the AMF.
    std::vector<std::uint8_t> transfer;
};

struct SetupResponse {
    long amf_ue_id = -1;
    long ran_ue_id = -1;
    std::vector<SetupResponseItem> setup;  // PDU Session Resource Setup Response List
    std::vector<SetupResponseItem> failed; // PDU Session Resource Failed to Setup List
};

// 9.2.1.2. Decodes a whole NGAP-PDU; nullopt unless it is a successfulOutcome of procedure code 29
// with both UE NGAP IDs.
std::optional<SetupResponse> decode_setup_response(const std::vector<std::uint8_t>& pdu_bytes);

} // namespace amf::ngap
