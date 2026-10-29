## ADR-0527: NEF monitoring notifications as TS 29.122 MonitoringEventReports (SPEC-06)

**Date:** 2026-10-24. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SPEC-06: the NEF's MonitoringNotification to the AF carried the UDM's
raw Nudm_EE report under an invented `_nudmEeReport` member and no `monitoringEventReports`
(DOC-02 recorded it as non-conformant in ADR-0317). The UDM's immediate reports in the
ee-subscription response were dropped. User direction 2026-10-24: map per 3GPP; spec gaps to
README.

**Decision.** `nef::af_monitoring_reports` (`nfs/nef/src/monitoring_mapping.{hpp,cpp}`), TS 29.522
4.4.2 (which applies TS 29.122 with UDM/AMF in place of HSS/MME):
- Every report: `monitoringType` = the AF subscription's; `eventTime` = `timeStamp`; the UE as
  `externalId` / `msisdn` from the report's GPSI, else the subscription's.
- LOSS_OF_CONNECTIVITY: `lossOfConnectReason` DEREGISTERED 6, MAX_DETECTION_TIME_EXPIRED 7, PURGED
  8, UNAVAILABLE_PERIOD 9 (TS 29.522 4.4.2: values 0-5 not applicable); an unknown reason is
  omitted.
- UE_REACHABILITY_FOR_DATA / _SMS: `reachabilityType` DATA / SMS, `maxUEAvailabilityTime` from the
  reachability report.
- LOCATION_REPORTING: `locationInfo.userLocation` = the 5G `UserLocation` (TS 29.122 LocationInfo
  has that member, so no conversion to EPC cell/TA strings).
- ROAMING_STATUS: `roamingStatus`, `plmnId` = `newServingPlmn`.
- CHANGE_OF_SUPI_PEI_ASSOCIATION: `pei` = `newPei` (TS 29.522 4.4.2), `imeiChange` IMEI / IMEISV
  from the PEI form.
- COMMUNICATION_FAILURE, AVAILABILITY_AFTER_DDN_FAILURE: common members only.
Callback: `MonitoringNotification{subscription, monitoringEventReports}`; nothing is sent when no
report maps. Create: the UDM's immediate `eventReports` -> the 201's `monitoringEventReport` (the
first; the member is single).

**Rejected alternatives.**
- *Keeping the raw report next to the mapped one.* Not in the schema (the review finding).
- *EPC-style `cellId` / `trackingAreaId` strings for 5G locations.* LocationInfo carries
  `userLocation` for exactly this; converting would lose the NR location.

**Disclosed.**
- TS 29.522 conditions the 6-9 loss-of-connectivity values on the
  "Loss_of_connectivity_notification" feature; this NEF negotiates no MonitoringEvent features and
  always uses them (the only values that apply to 5GC). Recorded in README "3GPP specification
  gaps".
- Not mapped: `idleStatusInfo` (Nudm_EE `idleStatusIndication` has a different shape),
  `failureCause` for COMMUNICATION_FAILURE, PDU session status, group-member changes and CN type
  change reports.
- Several immediate reports: only the first reaches the AF (single `monitoringEventReport`).
- The project's UDM sends only immediate ROAMING_STATUS reports; nothing posts to the callback yet.

**Verified (2026-10-24).** See below.
