## ADR-0535: UDM store bounds per store and per token sub, confirmed subscription expiry (SUS-02)

**Date:** 2026-10-26. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SUS-02 (suspected, Medium): the in-memory stores of the NEF, UDM and
UDR look uncapped and never expire. For the UDM this is confirmed at `dc02145`. Its 13 stores
(`nfs/udm/src/stores.hpp`) are unbounded `std::unordered_map`s:

- Every Nudm_UECM registration PUT stores one entry per `ueId`, and the `ueId` is not checked
  against subscription data.
- The SDM, shared-data and EE subscription stores keep every subscription until it is deleted.
- A Nudm_PP merge-patch creates the UE's document when it is absent.

User direction (2026-10-26):

- Bound each store, and also each consumer's share of it.
- For a full store, send the cause the API defines, else TS 29.500's `INSUFFICIENT_RESOURCES`.
- Identify the consumer by the verified access token's `sub`.
- Honour the spec's expiry attributes, and confirm a policy expiry when the consumer proposes none.
- Defaults: 100,000 entries per store, 10,000 per consumer, 24 h maximum lifetime, 60 s sweep.

This increment covers the UDM only. The NEF (18 stores) and the UDR follow.

**Decision:** every UDM store is bounded per store and per access-token `sub`, a create beyond
the bound answers 500 `INSUFFICIENT_RESOURCES`, and the UDM confirms and enforces each
subscription's expiry.

- **sbi-core**
  - `sbi_core::StoreQuota` / `StoreLimits` (`store_quota.hpp`) records who created each key of a
    store. It refuses a new key when the store holds `max_entries` keys, or when the creator holds
    `max_per_consumer` of them.
  - Replacing a key that is already held always succeeds, and the original creator stays its
    owner. A request without a token (`oauth2_required: false`) counts only toward
    `max_entries`.
  - `Authorizer::authorize` returns the accepted token's `sub`, and the server sets it in
    `http2::Request::token_sub` before calling the handler. `Authorizer::check` is unchanged for
    its callers.
- **UDM bounds**
  - All 13 stores take `store_max_entries` (100,000) and `store_max_entries_per_consumer` (10,000)
    from `config/udm.json`. They can be overridden with `UDM_STORE_MAX_ENTRIES` and
    `UDM_STORE_MAX_ENTRIES_PER_CONSUMER`.
  - A create, PUT or PP merge-patch that the quota refuses changes nothing and answers 500
    `INSUFFICIENT_RESOURCES` (TS 29.500 table 5.2.7.2-1, "The request is rejected due to
    insufficient resources"). TS 29.503 defines no cause for this: its only creation refusal,
    `CREATION_NOT_ALLOWED`, is "Creation of a 5G VN Group is not allowed". The YAML of each of the
    13 creating operations lists 500.
  - Deleting an entry, or expiring a subscription, releases its key.
- **MAP `updateLocation`** (the UDM's SS7 side, which has no token) is subject to the store bound
  only. When the AMF registration store is full, the update is refused with MAP `systemFailure`
  (TS 29.002 j10: `CODE local:34`, and `updateLocation` lists it among its ERRORS).
- **Expiry**
  - The SDM and shared-data subscriptions carry `expires` (TS 29.503 6.1.6.2.3: "Within a POST
    request the proposed expiry time is conveyed whereas in a POST response or PATCH response the
    confirmed expiry time is returned"). EE subscriptions carry `reportingOptions.expiry`
    (6.4.6.2.6).
  - The UDM confirms the proposed time when it is no later than now +
    `subscription_max_lifetime_seconds` (86,400). Otherwise, and when nothing is proposed, it sets
    now + that lifetime less a random share of up to a tenth of it. TS 29.503 5.5.2.2.2 says "The
    NF Service Producer shall not provide the same expiry time for many subscriptions".
  - A proposal accepted as is is returned in the consumer's own spelling.
  - A modify (SDM and shared-data merge-patch, EE JSON Patch) confirms the expiry again.
  - An expiry that is not an RFC 3339 date-time is answered 400 `OPTIONAL_IE_INCORRECT`, and the
    store is unchanged.
  - EE: when `maxNumOfReports` is 1 and the response carries the event report, the confirmed
    expiry is the current time. 6.4.6.2.6: "shall be an immediate timestamp".
  - A sweeper thread runs every `store_sweep_interval_seconds` (60). It removes subscriptions whose
    confirmed expiry has passed, which the clause calls the point at which "the subscription becomes
    invalid".
  - The lifetime and the interval can be overridden with `UDM_SUBSCRIPTION_MAX_LIFETIME_SECONDS` and
    `UDM_STORE_SWEEP_INTERVAL_SECONDS`. A non-positive value is a fatal configuration error.

**Disclosed.**

- The bound applies per store, so the UDM can hold up to 13 × `store_max_entries` entries in all.
- **Bounded but never expired.** No TS 29.503 attribute gives an expiry for these, so they stay
  until deleted:
  - registrations;
  - roaming-information updates;
  - authentication events;
  - PP data, whose `validityTime`s apply to individual parameters, not to the document.
- **The sweep is periodic.** A subscription can outlive its expiry by up to one interval. Its
  removal sends no notification, and the spec asks for none.
- **No refresh by the NEF and AAnF.** Neither renews its UDM EE subscription, so the UDM now drops
  those subscriptions within 24 h. This loses no reports: the UDM sends no EE notification after
  the immediate report it returns (ADR-0497), and the UDM does not forward EE subscriptions to the
  AMF or SMF. Both are existing gaps that this ADR does not change.
- **Not a store change.** Two other changes in the UDM:
  - `PpDataStore::put`, which nothing called, is removed.
  - The SMF and NWDAF registration stores now erase a UE's inner map when its last entry goes.
- **One existing test changed.** `test_udm_sdm_gap_closure_232` used to propose
  `2027-01-01T00:00:00Z`, which is beyond the 24 h lifetime. It now proposes now + 1 h.

**Rejected alternatives.**

- **429 or 503 for a full store.** TS 29.500 table 5.2.7.2-1 defines 429 `NF_CONGESTION_RISK` for
  "excessive traffic", which is a rate, not stored state. A full store is the 500 row's
  "insufficient resources".
- **The body's `nfInstanceId` as the consumer key.** The consumer writes it and can claim any value.
  The verified token's `sub` is set by the NRF (user decision).
- **Evicting the oldest entry when full.** That would drop another consumer's registration or
  subscription without telling it.
- **No expiry when none is proposed.** TS 29.503 lets the UDM set one "based on operator policy".
  Leaving such subscriptions unexpired keeps the growth this finding is about (user decision).
- **A timer per subscription.** That costs one timer per entry for a bound that a 60 s sweep already
  enforces closely enough.

**Verified (2026-10-26).**

- **Spec texts read:** TS 29.503 j70, TS 29.500 j71, TS 29.002 j10 (`specs/3gpp/`).
- **Syntax:** `g++` 13.3 and `clang++` 18.1.3 (`-std=c++23 -fsyntax-only` with the project warning
  flags) on the 9 changed or new translation units. No errors, and no warning on a changed line
  beyond the `TlsConfig` initializer pattern that every integration test's `make_client()` has.
- **Formatting:** `clang-format-18 --dry-run -Werror` is clean.
- **Generated files:** `gen_compose_config.py --check` and the GUI config-schema `--check` pass after
  regenerating `deploy/docker/lab/config/udm.json` and `udm.schema.json`.
- **Tests added but not run here** (there is no build tree in this environment):
  - `SbiStoreQuota.*`
  - `SbiConfirmExpiry.*`
  - `SbiAuthorizer.AnAcceptedTokensSubIsHandedOn`
  - `UdmStoreBounds.*` (per-consumer 500 and release; the confirmed expiry and the 400; the
    EE immediate timestamp; removal after expiry)
