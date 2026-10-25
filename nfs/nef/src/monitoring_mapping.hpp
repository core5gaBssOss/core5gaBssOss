#pragma once

// Nudm_EE MonitoringReport (TS 29.503, TS29503_Nudm_EE.yaml) -> the AF's MonitoringEventReport
// (TS 29.122 5.3, TS29122_MonitoringEvent.yaml), the NEF's side of TS 29.522 4.4.2 (ADR-0527,
// REVIEW-5ae0ef1 SPEC-06). Private to nfs/nef.

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

#include "TS29122_MonitoringEvent.hpp"

namespace nef {

struct AfMonitoringPlan {
    std::vector<sbi_gen::MonitoringEventReport> reports;
    std::vector<std::string> not_mapped; // Nudm_EE event types with no AF report in this build
};

// One MonitoringEventReport per Nudm_EE MonitoringReport in `udm_reports` (an array, or a single
// object). Every report: monitoringType = the AF subscription's own; eventTime = timeStamp; the UE
// as externalId / msisdn from the report's gpsi ("extid-" / "msisdn-"), else from the subscription.
// Per Nudm_EE eventType:
//   LOSS_OF_CONNECTIVITY: lossOfConnectReason DEREGISTERED / MAX_DETECTION_TIME_EXPIRED / PURGED /
//     UNAVAILABLE_PERIOD -> 6 / 7 / 8 / 9 (TS 29.522 4.4.2: values 0-5 are not applicable);
//   UE_REACHABILITY_FOR_DATA / _FOR_SMS: reachabilityType DATA / SMS, maxUEAvailabilityTime from
//     reachabilityReport / reachabilityForSmsReport.maxAvailabilityTime;
//   LOCATION_REPORTING: locationInfo.userLocation = report.location (the 5G UserLocation);
//   ROAMING_STATUS: roamingStatus = report.roaming, plmnId = report.newServingPlmn;
//   CHANGE_OF_SUPI_PEI_ASSOCIATION: pei = report.newPei (TS 29.522 4.4.2), imeiChange IMEI / IMEISV
//     from the PEI's "imei-" / "imeisv-" form;
//   COMMUNICATION_FAILURE, AVAILABILITY_AFTER_DDN_FAILURE: the common members only.
// Any other eventType goes to `not_mapped`.
AfMonitoringPlan af_monitoring_reports(const nlohmann::json& udm_reports,
                                       const sbi_gen::MonitoringEventSubscription& sub);

} // namespace nef
