#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace smf {

// ADR-0642: the downlink PDR matches the UE's IPv4 address as the destination (TS 29.244 V19.6.0
// 8.2.62, UE IP Address in the PDI with S/D = 1) when the SM context has one (ADR-0525 allocates
// it), and references the session QER, which carries the session's QFI (8.2.89), so the UPF can
// mark each downlink G-PDU with it (TS 29.244 5.4.2: "the mapping of DL traffic to QoS flows is
// achieved by configuring QERs with QFI(s) ... and associating FARs to the downlink PDRs"; TS
// 38.415 5.5.2.1). Before ADR-0642 the PDR matched SourceInterface=Core only and had no QER.
//
// One QER per session: the UPF datapath holds one QER per TEID (ADR-0071), so a second QER would
// replace the Session-AMBR one. The session QER (ADR-0308) therefore carries both the MBR and the
// QFI; when establishment installed none (no parsable AMBR), this message creates it with the QFI
// and no MBR.
//
// Disclosed: every call sends Create PDR 2 / Create FAR 2, also on a path switch where Update FAR
// would be the precise message (unchanged from ADR-0092/ADR-0259).
inline constexpr std::uint16_t kDlPdrId = 2;
inline constexpr std::uint32_t kDlFarId = 2;

// The IEs of the PFCP Session Modification Request that installs the downlink path towards the
// gNB's N3 F-TEID (gnb_teid, gnb_ipv4): Create PDR (referencing QER `qer_id`) and Create FAR, plus
// Create QER `qer_id` (gate open, QFI) when `create_qer` (TS 29.244 Table 7.5.4.1-1).
std::vector<std::uint8_t>
build_downlink_rule_ies(std::uint32_t gnb_teid,
                        const std::array<std::uint8_t, 4>& gnb_ipv4,
                        std::optional<std::array<std::uint8_t, 4>> ue_ipv4,
                        std::uint8_t qfi,
                        std::uint32_t qer_id,
                        bool create_qer);

} // namespace smf
