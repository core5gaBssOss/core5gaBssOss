#pragma once

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "li_poi/poi_runtime.hpp"
#include "sms_core/sms.hpp"

// The SMSF's Lawful Interception IRI-POI (TS 33.127 clause 6.2.5, TS 33.128 clause 6.2.5,
// ADR-0486). Private to nfs/smsf. Shares the generic X1/X2 runtime (libs/li-poi) with the AMF, SMF
// and UDM POIs; what stays here is SMSF-specific: target matching on SUPI/PEI/GPSI (table
// 6.2.5.1 target formats), the SMSMessage and SMSReport records, and TPDU redaction when a task
// carries TruncateTPUserData (table 6.2.5.1-2, clause 7.4.5.2).
//
// Disabled unless config/smsf.json's li_poi.enabled is true; when disabled main.cpp holds a
// nullptr and the SMSF behaves exactly as without it. Delivery is best-effort, the convention of
// every POI here: a dead MDF2 is logged, never allowed to break the SMSF procedure.
//
// Wired (clause 6.2.5.3): the SMS-MO case for a TARGET UE -- SMSMessage when the SMSF receives
// CP-DATA/RP-DATA [SMS-SUBMIT, SMS-COMMAND] in Nsmsf_SMService_UplinkSMS, and SMSReport when it
// sends the CP-DATA/RP-ERROR [SMS-SUBMIT-REPORT] back via Namf_Communication_N1N2MessageTransfer.
// NOT wired (disclosed): the SMS-MT case (MAP MT-FORWARD-SHORT-MESSAGE -- no SMS-GMSC interworking
// in this project yet), and "target non-Local ID" matching on TP-DA/TP-OA (NOTE 1: a different
// identification method this project's X1 codec does not carry).

namespace smsf {

struct LiIdentities {
    std::string supi; // SBI form, "imsi-..." / "nai-..."
    std::optional<std::string> pei;
    std::optional<std::string> gpsi;
};

class SmsfLiPoi {
public:
    explicit SmsfLiPoi(li_poi::Config config);
    ~SmsfLiPoi();
    SmsfLiPoi(const SmsfLiPoi&) = delete;
    SmsfLiPoi& operator=(const SmsfLiPoi&) = delete;

    void start();
    void stop();

    // SMSMessage for an SMS-MO from a target UE (direction fromTarget). `ue_location` is the
    // UplinkSMS request's own ueLocation (TS 29.571 UserLocation JSON), NOTE 7. Returns the tasks
    // it was reported under -- the SMSReport for the reply is sent under exactly these ("in
    // response to a previously intercepted CP-DATA_RP-DATA").
    std::vector<li_poi::Match>
    report_mo_sms_message(const LiIdentities& ids,
                          const sms_core::RpMessage& rp_data,
                          const sms_core::MoTpdu& tpdu,
                          const std::optional<nlohmann::json>& ue_location);

    // SMSReport for the RP-ACK/RP-ERROR the SMSF sent back. Skipped (logged) if the reply carries
    // no TPDU -- sMSTPDUData is Mandatory in table 6.2.5.3-2 and is never fabricated.
    void report_sms_report(const std::vector<li_poi::Match>& matches,
                           const sms_core::RpMessage& rp_reply);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace smsf
