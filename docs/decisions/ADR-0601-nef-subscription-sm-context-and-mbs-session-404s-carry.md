## ADR-0601: NEF subscription, SM context and MBS session 404s carry their spec cause (SBI-03, NEF part 2)

**Date:** 2026-11-20. **Status:** Proposed; the GET reading and the four open candidates accepted (user, 2026-11-21).

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), NEF part 2.
ADR-0578 gave the AM-policy context 404s their cause and left the NEF's other 171 cause-less 404s
open, because each route had to be checked against its own API text rather than matched on its
error wording. This ADR does that check, site by site, against the texts in `specs/3gpp/`
(TS 29.122 j70, TS 29.522 j70, TS 29.541 j30, TS 29.551 j40, TS 29.591 j70, TS 29.500 j70) and
the resource tags of the REL-19 YAMLs.

What the specifications define for a 404:

- TS 29.122 clause 5.2.6 and TS 29.522 apply the TS 29.500 table 5.2.7.2-1 errors to every T8 and
  AF-facing API. The table's only 404 for a missing resource is `SUBSCRIPTION_NOT_FOUND`
  ("modification or deletion of a subscription, or the notification request, is rejected because
  the subscription is not found").
- API-specific 404 causes are in TS 29.522 table 5.20.7.3-1 (MBSSession:
  `MBS_SESSION_CONTEXT_NOT_FOUND`, plus `UNKNOWN_TMGI`, `UNKNOWN_MBS_SERVICE_AREA`), table 5.6.5.3-1
  (AnalyticsExposure: `SUBSCRIPTION_NOT_FOUND` and three data causes), table 5.17.5.3-1 (ADR-0578),
  table 5.25.7.3-1 (UEId); TS 29.122 table 5.6.5.3-1 (NIDD: `ALREADY_DELIVERED`,
  `PORT_NOT_ASSOC_WITH_APP`) and tables 5.3.5.3-1, 5.10.5.3-1, 5.13.5.3-1 (UE address causes); and
  TS 29.541 table 6.1.7.3-1 (Nnef_SMContext: `CONTEXT_NOT_FOUND`, "the SM Context referred by the
  requested resource URI does not exist in the NEF").
- TS 29.551 (Nnef_PFDmanagement) and TS 29.591 (the other Nnef_* services) define no 404 cause.

**Decision:** 50 of the 171 sites get a cause; 121 keep none.

- `SUBSCRIPTION_NOT_FOUND` (TS 29.500 table 5.2.7.2-1), 45 sites:
  - every method on 8 subscription resources that had no cause on any method (the YAML tags each
    one "Individual ... Subscription"): 5GLANParameterProvision, ACSParameterProvision,
    ApplyingBdtPolicy, PDTQPolicyNegotiation, UEAddress, MBSSession subscriptions, CpProvisioning
    (the subscription, not its `cpSets`), TimeSyncExposure (the subscription, not its
    `configurations`): 23 sites;
  - the GET of 21 subscription resources whose PUT/PATCH/DELETE already carried it (AF-facing:
    AMInfluence, AnalyticsExposure, AsSessionWithQoS, ResourceManagementOfBdt, DNAIMapping,
    ImsEventExposure, MonitoringEvent, MSEventExposure, MemberUESelectionAssistance,
    NetStatusReport, ServiceParameter, TrafficInfluence, VFLInference, VFLTraining; Nnef_*:
    DNAIMapping, EASDeployment, ECSAddress, EventExposure, TrafficInfluenceData, VFLInference,
    VFLTraining): 21 sites;
  - the monitoring notification callback (`/nnef-callback/v1/monitoring-notify/...`) for a
    subscription the AF has deleted, the "notification request" case of the table: 1 site.
- `CONTEXT_NOT_FOUND` (TS 29.541 table 6.1.7.3-1): Nnef_SMContext `release`, `update` and
  `deliver` of an unknown SM context, 3 sites.
- `MBS_SESSION_CONTEXT_NOT_FOUND` (TS 29.522 table 5.20.7.3-1): DELETE and PATCH of an unknown
  `mbs-sessions/{mbsSessionRef}`, 2 sites.
- The other 121 keep `std::nullopt`. They are configurations, provisionings, transactions,
  deliveries, sessions and the PFD application GET. Their APIs define no 404 cause, and
  TS 29.500 table 5.2.7.2-1 has none for a missing non-subscription resource.
- A comment above the route constants in `nfs/nef/src/main.cpp` records this rule.
- Test: `NefSbi03Causes.UnknownResourcesCarryTheirSpecCause` (real NRF and NEF, real tokens) sends
  14 requests covering each group and asserts 404 and the cause.

**Disclosed.**

- **GET is an extension of the table's wording.** TS 29.500 names modification, deletion and
  notification, not retrieval. The GET sites follow ADR-0589 (SMF) and ADR-0590 (NWDAF), which did
  the same so that one resource answers one cause. If that reading is not wanted, the 21 GET-only
  sites plus the 8 GETs of the newly covered resources (29 in all) can be reverted without touching
  the rest. User, 2026-11-21: keep the reading ("keep reading").
- **Candidates deliberately left without a cause** (user, 2026-11-21: agreed, they stay cause-less):
  - NIDD `downlink-data-deliveries/{id}` PUT/PATCH/DELETE: TS 29.122 4.4.5.3.1 answers 404
    `ALREADY_DELIVERED` once buffered data has been delivered. This NEF cannot tell "already
    delivered" from "never existed", and the cause applies only with the
    MT_NIDD_modification_cancellation feature.
  - NIDD `rds-ports/{portId}` DELETE: TS 29.122 4.4.5.6.1 answers `PORT_NOT_ASSOC_WITH_APP` for a
    missing ManagePort Configuration, but table 5.6.5.3-1 limits it to the Rds_dynamic_port
    feature, which this NEF does not negotiate.
  - AMPolicyAuthorization `events-subscription` DELETE (ADR-0578 left it too): the store lookup
    does not say whether the context or its events subscription is missing, so neither
    `APPLICATION_AM_CONTEXT_NOT_FOUND` nor `SUBSCRIPTION_NOT_FOUND` is certain.
  - The three UEId lookups (`retrieve`, `get-msisdn`, `verify-msisdn`) answer 404 because this
    deployment has no IP-to-identity source. `UE_NOT_FOUND`/`UE_ID_NOT_AVAILABLE` describe a lookup
    that ran and found nothing, so they would misstate the reason.
- Found in passing, not changed: one UEId `pp/{ppId}` DELETE site already sends
  `SUBSCRIPTION_NOT_FOUND`, but its resource is a UE ID mapping provisioning, not a subscription.
  The MBSSession store also keys sessions and subscriptions in one map, so the two resources
  share ids.
- Targeted test only, not a full `ctest`. See Verified.

**Verified (2026-11-21, master machine).** `nef`, `nrf` and `integration_tests` build clean (0
failed steps). Run from the main checkout (which has `certs/`) at this commit:
`integration_tests --gtest_filter='NefSbi03Causes.*'` passes (1 test, 14 requests). With
`nfs/nef/src/main.cpp` reverted to `main` cc995945 and `nef` rebuilt, the test fails: the 404s
come back with an empty `cause` where `SUBSCRIPTION_NOT_FOUND` is expected. Targeted test only, not
a full `ctest`. CI result to be added.

**Rejected alternatives.**

- *`SUBSCRIPTION_NOT_FOUND` on every "No such ..." 404.* A configuration or provisioning is not a
  subscription, and the table's cause would be invented for it (CLAUDE.md guardrail 1). ADR-0578
  rejected the same.
- *Leaving the GETs cause-less while PUT and DELETE on the same resource send the cause.* That
  would contradict ADR-0589 and ADR-0590 for no gain.
- *Mapping `ALREADY_DELIVERED`/`PORT_NOT_ASSOC_WITH_APP` now.* Both are conditional on features
  and states this NEF does not track (above).
