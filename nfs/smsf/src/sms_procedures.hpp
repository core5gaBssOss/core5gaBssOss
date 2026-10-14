#pragma once

// MO SMS over NAS at the SMSF (TS 23.502 clause 4.13.3.3, ADR-0486). Pure: no I/O, so the TS
// 24.011 / TS 23.040 behaviour is unit-tested on its own; main.cpp sends what this returns.
//
// What the SMSF does with an uplink SMS payload (the binary part of Nsmsf_SMService_UplinkSMS,
// i.e. this API's `sendsms` resource):
//   CP-DATA                  -> CP-ACK to the UE (step 2c) and an RP reply (steps 6a-6b)
//     RP-DATA [SMS-SUBMIT / SMS-COMMAND]
//                            -> RP-ERROR carrying an SMS-SUBMIT-REPORT (clause 9.2.2.2a(i))
//     RP-SMMA                -> RP-ERROR, no TPDU
//     RP-ACK / RP-ERROR      -> nothing more (MT-direction acknowledgements; MT is a later
//     increment)
//   CP-ACK / CP-ERROR        -> nothing (step 6c-6d closes the CM transaction)
//
// Why RP-ERROR, never RP-ACK -- disclosed, not a shortcut: steps 3-5 hand the SMS-SUBMIT to an
// SMS-IWMSC (MAP MO-FORWARD-SHORT-MESSAGE, TS 29.002), and this project has no SMS-IWMSC / SC.
// An RP-ACK would tell the UE its message reached a Service Centre, which would be false. The
// RP-Cause and TP-FCS are configuration (config/smsf.json `mo_sms`), defaulting to #38 "Network
// out of order" (TS 24.011 table 8.4 part 1) and 0xC2 "SC system failure" (TS 23.040 clause
// 9.2.3.22).

#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <tl/expected.hpp>
#include <vector>

#include "sms_core/sms.hpp"

namespace smsf {

struct MoSmsPolicy {
    std::uint8_t no_iwmsc_rp_cause = 38;
    std::uint8_t no_iwmsc_tp_failure_cause = 0xC2;
};

struct UplinkOutcome {
    sms_core::CpMessage cp;
    std::optional<sms_core::RpMessage> rp;       // CP-DATA only
    std::optional<sms_core::MoTpdu> tpdu;        // RP-DATA only
    std::vector<sms_core::Bytes> downlink;       // SMS payloads for the UE, in send order
    std::optional<sms_core::RpMessage> rp_reply; // the RP message inside the last downlink CP-DATA
    std::optional<std::string> rp_error;         // CP-DATA acknowledged but its RPDU did not decode
};

// Errors only when the CP layer itself does not decode (then nothing can be acknowledged).

tl::expected<UplinkOutcome, std::string>
process_uplink_sms(std::span<const std::uint8_t> payload,
                   const MoSmsPolicy& policy,
                   std::chrono::system_clock::time_point now);

} // namespace smsf
