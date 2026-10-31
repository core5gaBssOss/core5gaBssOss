## ADR-0538: NEF sweeps the expiries its APIs make mandatory (SUS-02)

**Date:** 2026-10-31. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SUS-02: the NEF's stores never expire anything. ADR-0536 bounded
them and left expiry open. The NEF code ignored every expiry attribute.

The 2026-10-30 inventory of TS 29.122 j70, TS 29.522 j70 and TS 29.591 j70 found five
attributes whose expiry the NEF *shall* act on. The user decided on 2026-10-31: "implement the
five mandatory ones".

| Resource | Attribute | Text |
|---|---|---|
| TS 29.122 MonitoringEvent subscription | `monitorExpireTime` | 4.4.2.3: "the SCEF shall delete the related event subscription and event configuration locally" |
| TS 29.122 CpProvisioning | `CpParameterSet.validityTime` | 5.10.2.2.2: "expires and shall be deleted" |
| TS 29.122 NpConfiguration | `validityTime` | 5.13.2.1.2: "shall be deleted locally if it expires" |
| TS 29.522 5GLANParameterProvision | `NpConfigParams.validityTime` | 5.7.2.3.10: the same text |
| TS 29.522 TimeSyncExposure subscription | `expiry` | 5.15.4.3.2: "the subscription becomes invalid" |
| TS 29.591 Nnef_EventExposure subscription | `eventsRepInfo.monDur` | 4.2.2.2.2: "NEF selected expiry time" |

**Decision.**

- **One sweeper** (`std::jthread` in `nfs/nef/src/main.cpp`), every `store_sweep_interval_seconds`.
  - It runs `nfs/nef/src/expiry.cpp`'s decision per document over the stores. These are pure
    functions, unit-tested without a NEF.
  - `sweep()` on `NefEventExposureSubStore`, `AfMonitoringSubStore` and `AfDocumentStore` runs the
    decision under the store's lock. A removed entry releases its ADR-0536 quota.
- **What is removed:**
  - The whole resource, for monitoring, event exposure, time sync and NpConfiguration.
  - For 5GLAN, only the `npConfigParams` member; the 5G LAN provisioning stays.
  - For CP provisioning, the expired `CpParameterSet`s. A CpInfo left with none is removed,
    because `cpParameterSets` is required with minItems 1. A set stored on its own is removed.
- **Monitoring at the UDM:**
  - The confirmed `monitorExpireTime` is sent to the UDM as `ReportingOptions.expiry` of the
    Nudm_EE subscription (TS 29.503).
  - When the NEF sweeps the AF's subscription, it also DELETEs the UDM ee-subscription. A failed
    DELETE is logged; the UDM's own expiry ends it anyway (ADR-0535).
- **Capping a proposed expiry:** `monitorExpireTime`, `monDur` and the time-sync `expiry` are
  confirmed no later than now + `subscription_max_lifetime_seconds` on POST, PUT and (monitoring)
  PATCH.
  - This uses `sbi_core::confirm_expiry`, as the UDM does, including the spread below the cap.
  - The texts allow it: TS 29.591 "equal or less than the expiry time received in the request";
    TS 29.522 5.15.4.3.2 "should be less than or equal"; TS 29.522 4.4.2 "the NEF may determine a
    monitoring expiry time, based on operator policy".
  - An absent expiry is **not** added. A `monitorExpireTime` makes a monitoring request continuous
    (TS 29.122 NOTE 2 of 5.3.2.1.2), and TS 29.522 5.15.4.3.2 says "If the expiry time is not
    included in the response, the AF shall not associate an expiry time".
- **Configuration:** `subscription_max_lifetime_seconds` (86400) and `store_sweep_interval_seconds`
  (60) in `config/nef.json`, overridable as `NEF_SUBSCRIPTION_MAX_LIFETIME_SECONDS` and
  `NEF_STORE_SWEEP_INTERVAL_SECONDS`. The UDM uses the same defaults. Compose, Helm and GUI schema
  are regenerated.

**Disclosed.**

- **Expiry is late by up to one sweep interval** (60 s by default). Until then a GET still answers
  200.
- **No notification on expiry.** None of the five texts asks for one, so none is sent.
- **Time-sync configurations are not cascaded.** An expired TimeSyncExposureSubsc's child
  configurations stay, as they do when the subscription is DELETEd (a gap that predates this ADR).
- **NpExpiry_5G is not negotiated.** The NEF echoes `supportedFeatures` on NpConfiguration
  without negotiating it, which predates this ADR. A `validityTime` is honoured whenever present.
- **Not swept** (the specs say "may", or the attribute is not a lifetime):
  - NIDD duration, TMGI expiration, BDT/PDTQ window end;
  - per-event `eventRepInfo.monDur`;
  - AKMA key and analytics `expiry`; device-trigger `validityPeriod`; `policyDuration`.
- **Other APIs carrying `ReportingInformation.monDur`** (AnalyticsExposure, DNAI mapping, VFL,
  TrafficInfluence, Nnef training/inference) were not checked for an expiry obligation. They are
  not swept.

**Rejected alternatives.**

- **Per-entry timers.** One thread per store or per entry instead of a sweep. A periodic sweep is
  what the UDM does (ADR-0535), and bounded stores keep a full scan cheap.
- **Assigning a maximum lifetime to every subscription** (the UDM's choice for TS 29.503). It
  would add a `monitorExpireTime` the AF did not ask for, turning a one-time monitoring request
  into a continuous one, against the TS 29.522 5.15.4.3.2 text quoted above.
- **Including the "may delete" resources.** The user chose the mandatory five.

**Verified (2026-10-31).**

- **Spec texts read:** as cited above (TS 29.122 j70, TS 29.522 j70, TS 29.591 j70), and YAML
  `TS29591_Nnef_EventExposure.yaml`, `TS29122_NpConfiguration.yaml`, `TS29122_CpProvisioning.yaml`,
  `TS29522_5GLANParameterProvision.yaml`, `TS29522_TimeSyncExposure.yaml` and
  `TS29503_Nudm_EE.yaml` ReportingOptions.
- **Syntax:** `g++-15 -fsyntax-only` with the build's flags on `nfs/nef/src/main.cpp`,
  `stores.cpp`, `expiry.cpp`, `tests/conformance/test_nef_expiry.cpp` and
  `tests/integration/test_nef_expiry.cpp`. No errors and no new warnings.
- **Formatting:** `clang-format-18` applied. The generators' `--check` passes.
- **Tests added but not run here** (the CI runner shares this host and was busy):
  - `NefExpiry.*` (conformance, 7): each decision and the cap.
  - `NefExpiry.ExpiredResourcesAreSweptAndCurrentOnesStay` (integration): NpConfiguration, event
    exposure, time sync and a CP set gone after a 1 s sweep; an unexpired configuration and set
    kept.
  - `NefExpiry.AProposedExpiryBeyondTheMaximumLifetimeIsCapped` (integration).
