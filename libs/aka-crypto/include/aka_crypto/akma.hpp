#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "aka_crypto/kdf.hpp"

// AKMA identifiers (ADR-0495). Shared by the AUSF (which builds the A-KID) and the AAnF (which
// parses AF identifiers); no NF includes another's private headers.

namespace aka_crypto {

// A-KID (TS 33.535 6.1: NAI "username@realm", username with the RID and the A-TID, realm the Home
// Network Identifier). The exact encoding is not defined by TS 33.535 j00 nor TS 23.003 j80; by the
// user's decision (ADR-0495) it mirrors TS 23.003 28.7.11's CP-PRUK ID:
//   "rid<RID>.tid<A-TID as 64 lowercase hex digits>@5gc.mnc<MNC>.mcc<MCC>.3gppnetwork.org"
// with the MNC padded to three digits (TS 23.003 28.2).
std::string make_a_kid(const std::string& routing_indicator,
                       const ATid& a_tid,
                       const std::string& mcc,
                       const std::string& mnc);

// TS 29.522 AfId "<FQDN>.<Ua* security protocol id>" (10 hex digits, TS 33.220 Annex H) -> the
// AF_ID octets of TS 33.535 A.4: FQDN || the 5 protocol-id octets. nullopt if malformed.
std::optional<std::vector<uint8_t>> af_id_octets(const std::string& af_id);

} // namespace aka_crypto
