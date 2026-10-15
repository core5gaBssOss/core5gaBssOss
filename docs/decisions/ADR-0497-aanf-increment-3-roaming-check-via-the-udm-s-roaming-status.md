## ADR-0497: AAnF increment 3 -- roaming check via the UDM's ROAMING_STATUS report (TS 33.535 6.2.1 steps 5-7)

**Date:** 2026-10-15. **Status:** Accepted -- built.

**Context.** TS 33.535 6.2.1 steps 5-7: the AAnF subscribes at the UDM (Nudm_EventExposure_Subscribe,
RoamingStatusReport) and, by local policy, refuses AKMA to a roaming UE -- TS 29.535's
RoamingRestriction feature, error ROAMING_AKMA_SERVICE_DENIED. `nfs/udm`'s Nudm_EE stored
subscriptions but never produced a report (ADR-0082).

**Decisions.**
1. UDM: an EE subscription whose monitoringConfigurations carry eventType ROAMING_STATUS with
   immediateFlag gets an immediate MonitoringReport in CreatedEeSubscription.eventReports, with
   report = RoamingStatusReport {roaming, newServingPlmn, accessType 3GPP_ACCESS}. The serving PLMN
   is the UE's AMF registration's guami.plmnId (Nudm_UECM); roaming = it is not one of the UDM's
   `home_plmns` (config, the lab subscribers' 999/70). No AMF registration on record: no report.
2. AAnF: with `deny_roaming_ues`, before deriving KAF it subscribes for the SUPI (ROAMING_STATUS,
   immediateFlag), reads the report, deletes the subscription, and answers a roaming UE 403
   ROAMING_AKMA_SERVICE_DENIED. No report (UE not registered): AKMA provided, logged.

**Disclosed.**
- The UDM sends no later ROAMING_STATUS notifications (its EE layer has no change detection), so
  the AAnF does not keep the subscription the spec intends to stay ("when the roaming status
  changes, the UDM also sends a notification"): it asks per request and unsubscribes. Consequently
  the AAnF does not track transmitted A-KIDs per AF (step 8's NOTE) and no 6.8 service-disable
  notification follows a roaming change.
- Only 3GPP access (amf-3gpp-access) is considered; dual registration (non-3GPP) is not.
- The EE subscription is addressed by SUPI; GPSI-addressed ROAMING_STATUS is not reported.

**Verified.** `AkmaEndToEnd.AusfRegistersKakmaAndTheAanfGivesTheAfTheSameKafTheUeDerives`, extended:
the test as the AMF registers the UE in 999/70 (AKMA provided), then in 001/01 (403
ROAMING_AKMA_SERVICE_DENIED), then back.
