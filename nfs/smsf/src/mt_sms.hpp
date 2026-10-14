#pragma once

// MT SMS over NAS at the SMSF (TS 23.502 clauses 4.13.3.6-4.13.3.7, TS 23.540 clauses 5.1.2 and
// 5.1.5, ADR-0486 increment 4): Nsmsf_SMService_MtForwardSm, this API's `send-mt-sms` resource.
//
//   SMS-GMSC --MtForwardSm [CP-DATA(RP-DATA [SMS-DELIVER / SMS-STATUS-REPORT])]--> SMSF
//   SMSF --Namf_MT_EnableUEReachability--> AMF                               (step 4)
//   SMSF --N1N2MessageTransfer CP-DATA(RP-DATA)--> AMF --> UE                (steps 5a-5b)
//   UE --> AMF --UplinkSMS CP-ACK--> SMSF                                    (steps 5c-5d)
//   UE --> AMF --UplinkSMS CP-DATA(RP-ACK / RP-ERROR [SMS-DELIVER-REPORT])--> SMSF (6a-6b)
//   SMSF --N1N2MessageTransfer CP-ACK, lastMsgIndication--> AMF --> UE       (steps 6c-6d)
//   SMSF --MtForwardSm 200 [delivery report]--> SMS-GMSC                     (step 7)
//
// The MtForwardSm request is answered only when the UE's delivery report is in (TS 29.540 clause
// 5.2.2.5.2 step 2a: "200 OK ... contains the MT SMS Delivery Report"), so the request handler
// blocks on MtTransactions::run. Disclosed: the SBI server has no deferred-response API, so each
// MT SMS in flight holds one server worker thread for up to TR1N; `max_concurrent` caps that, and
// past it the SMSF answers 503 NF_CONGESTION (TS 29.500 table 5.2.7.2-1) instead of starving the
// pool the AMF's UplinkSMS calls need.
//
// Disclosed interpretations (no clause states them):
// - Payload layer. TS 29.540 clause 6.1.6.4.2 says the vnd.3gpp.sms part "may encode e.g. CP-DATA,
//   CP-ACK, CP-ERROR". The request is taken as CP-DATA carrying RP-DATA (network -> MS), the
//   delivery report returned as CP-DATA (the reply direction of the request's own TI) carrying the
//   UE's RP-ACK / RP-ERROR unchanged. The RP-DATA (RP-MR, RP-OA = SC address, TPDU) is relayed to
//   the UE as received; only the CM-layer TI is the SMSF's own.
// - TR1N. TS 24.011 clause 10 sets TR1M (35-45 s) for the MS but no network-side value; TR1N is
//   configuration (config/smsf.json `mt_sms.tr1n_seconds`, default 40). TC1* retransmission of
//   the CP-DATA is not implemented: a missing CP-ACK simply runs into TR1N.
// - TS 24.011 clause 3.2: "parallel message transfer in the same direction is prohibited", so a
//   second MT SMS for
//   a UE waits for the first (within its own TR1N) rather than running alongside it.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <tl/expected.hpp>
#include <unordered_map>

#include "sms_core/sms.hpp"

namespace smsf {

struct MtSmsPolicy {
    std::chrono::milliseconds tr1n{std::chrono::seconds(40)};
    std::size_t max_concurrent = 4;
};

// The MtForwardSm payload, validated.
struct MtRequest {
    sms_core::CpMessage cp; // as received from the SMS-GMSC
    sms_core::RpMessage rp; // RP-DATA network -> MS
    sms_core::MtTpdu tpdu;  // SMS-DELIVER or SMS-STATUS-REPORT
};

// Errors map to 400 SMS_PAYLOAD_ERROR (TS 29.540 table 6.1.7.3-1).
tl::expected<MtRequest, std::string> parse_mt_forward_payload(std::span<const std::uint8_t> in);

// The CP-DATA the SMSF sends to the UE: the request's RPDU under the SMSF's own TI (TI flag 0,
// the SMSF originates this CM transaction -- TS 24.007 table 11.3).
sms_core::Bytes make_mt_cp_data(const MtRequest& request, std::uint8_t ti_value);

// The delivery report for the MtForwardSm response: CP-DATA, reply direction of the request's own
// CP-DATA, carrying the UE's RPDU (RP-ACK or RP-ERROR, MS -> network) as received.
sms_core::Bytes make_delivery_report_payload(const MtRequest& request,
                                             const sms_core::Bytes& ue_rpdu);

enum class MtFailure {
    Congestion,   // max_concurrent MT transactions already in flight
    NotDelivered, // the AMF did not accept the downlink CP-DATA (UE not reachable)
    CpError,      // the UE answered with CP-ERROR
    Timeout,      // TR1N expired without a delivery report
};

struct MtReport {
    sms_core::RpMessage rp;       // RP-ACK or RP-ERROR, MS -> network
    sms_core::Bytes rpdu;         // the same, as received
    std::string ue_location_json; // the carrying UplinkSMS's ueLocation (empty if none)
};

class MtTransactions {
public:
    explicit MtTransactions(MtSmsPolicy policy) : policy_(policy) {}

    // Runs one MT transfer for `supi`: reserves the UE's MT slot, calls `send_downlink(ti)` (which
    // must hand the CP-DATA to the AMF and arrange for downlink_result() to be called), then waits
    // for the UE's delivery report or a failure. Blocks for at most TR1N (plus the wait for an
    // earlier MT transfer to the same UE, inside the same TR1N budget).
    tl::expected<MtReport, MtFailure> run(const std::string& supi,
                                          const std::function<void(std::uint8_t)>& send_downlink);

    // The AMF's answer to the downlink CP-DATA.
    void downlink_result(const std::string& supi, std::uint8_t ti_value, bool accepted);

    // An uplink SM-CP message from the UE on a network-originated TI (TI flag 1). Returns whether
    // an MT transaction took it. CP-ACK advances the transaction; CP-DATA must carry RP-ACK or
    // RP-ERROR (MS -> network) and completes it; CP-ERROR fails it.
    bool on_uplink(const std::string& supi,
                   const sms_core::CpMessage& cp,
                   const std::optional<sms_core::RpMessage>& rp,
                   const std::string& ue_location_json = {});

    std::size_t in_flight() const;

private:
    struct Slot {
        bool busy = false;
        std::uint8_t ti_value = 0;
        std::uint8_t next_ti = 0;
        bool cp_acked = false;
        std::optional<MtFailure> failure;
        std::optional<MtReport> report;
    };

    MtSmsPolicy policy_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::unordered_map<std::string, Slot> slots_;
    std::size_t in_flight_ = 0;
};

} // namespace smsf
