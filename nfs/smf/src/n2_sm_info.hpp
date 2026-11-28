#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// ADR-0642: the PDU Session Resource Setup Request Transfer (TS 38.413 V19.4.0 9.3.4.1), the N2
// SM information the SMF hands the AMF in TS 23.502 4.3.2.2.1 step 11 and in N2 handover
// preparation (TS 23.502 4.9.1.3.2 step 3, HANDOVER_REQUIRED). One builder for both, so the QoS
// flow the gNB is told about is the one the UE's NAS Accept and the UPF's downlink QER carry.
namespace smf::n2 {

// One QoS flow of the QoS Flow Setup Request List (TS 38.413 9.3.1.12): a non-dynamic 5QI and
// its ARP. Only the default QoS flow is set up; this build has no dedicated QoS flows.
struct QosFlowSetup {
    std::uint8_t qfi = 1;        // 9.3.1.51, 0..63
    long five_qi = 9;            // 9.3.1.28, NonDynamic5QIDescriptor
    long arp_priority_level = 8; // 9.3.1.19, 1..15
    bool arp_may_trigger_pre_emption = false;
    bool arp_pre_emptable = false;
};

struct SetupRequestTransferInput {
    // UL NG-U UP TNL Information (M): the UPF's N3 F-TEID allocated at N4 Session Establishment.
    std::uint32_t ul_teid = 0;
    std::array<std::uint8_t, 4> ul_ipv4{};
    QosFlowSetup flow;
    // PDU Session Aggregate Maximum Bit Rate (O, "shall be present when at least one Non-GBR QoS
    // flow is being setup"), in bit/s (TS 38.413 9.3.1.4 BitRate). Present only when both are
    // known.
    std::optional<std::uint64_t> ambr_dl_bps;
    std::optional<std::uint64_t> ambr_ul_bps;
};

// Aligned-PER bytes of the transfer, or empty on an encoding failure. PDU Session Type is IPv4:
// this build allocates only IPv4 UE addresses (ADR-0525).
std::vector<std::uint8_t> encode_setup_request_transfer(const SetupRequestTransferInput& in);

} // namespace smf::n2
