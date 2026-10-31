## ADR-0536: NEF store bounds per store and per token sub; TS 29.122 QUOTA_EXCEEDED where defined (SUS-02)

**Date:** 2026-10-30. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SUS-02 (suspected, Medium) says the in-memory stores of the NEF, UDM
and UDR look uncapped and never expire. ADR-0535 bounded the UDM. This ADR covers the NEF.

The NEF case is confirmed at `f475eac`. `nfs/nef/src/stores.hpp` has 18 store classes, and
`main.cpp` instantiates 65 stores from them:

- 12 back the nnef-* SBI APIs. One of them, the PFD catalog, is seed-only.
- 53 back the northbound TS 29.122 / TS 29.522 APIs.

Every store is an unbounded `std::unordered_map`. Any POST to one of the 66 create routes adds an
entry, and the entry stays until it is deleted.

This ADR applies the ADR-0535 user direction (2026-10-26) to the NEF:

- Bound each store, and each consumer's share of it.
- The consumer is the verified access token's `sub`.
- For a full store, send the cause the API defines, else TS 29.500's `INSUFFICIENT_RESOURCES`.

**Decision:** every NEF store that a request can add to is bounded per store and per access-token
`sub`. A create beyond the bound stores nothing and is answered with the API's own cause where one
is defined, else 500 `INSUFFICIENT_RESOURCES`.

- **sbi-core**
  - `StoreQuota::try_admit` returns the reason for a refusal: `Refusal::store_full` or
    `Refusal::consumer_full`.
  - `admit()` keeps its boolean form, so the UDM is unchanged.
- **NEF stores**
  - The 17 mutable store classes take `sbi_core::StoreLimits`.
  - `create` returns `std::expected<std::string, sbi_core::Refusal>` (CLAUDE.md: `std::expected`
    for recoverable errors).
  - `AfDocumentStore::create_with_id` returns `std::expected<bool, sbi_core::Refusal>`. It is the
    PUT-creates path of the NIDD rds-ports and the AM PolicyAuthorization events-subscription.
  - `remove` releases the key.
  - `put`, `merge_patch`, `json_patch` and `update` only replace an existing key since ADR-0533,
    so they need no admission.
- **Limits** are `store_max_entries` (100,000) and `store_max_entries_per_consumer` (10,000) in
  `config/nef.json`, the same defaults as the UDM. They can be overridden with
  `NEF_STORE_MAX_ENTRIES` and `NEF_STORE_MAX_ENTRIES_PER_CONSUMER`. They appear in the GUI config
  schema (regenerated, not hand-written), the compose lab config and the Helm chart config.
- **Cause**
  - TS 29.122 5.2.6 makes TS 29.500 5.2.7.2's causes apply to the northbound APIs. A full store is
    500 `INSUFFICIENT_RESOURCES` ("The request is rejected due to insufficient resources") on every
    API, and so is a consumer over its share.
  - The exception is the two creates whose TS 29.122 procedure defines a cause for an SCS/AS over
    its quota:
    - 4.4.8.2, Network Status Reporting subscription create: "if the SCS/AS has exceeded its quota
      of submitting requests ... 403 Forbidden ... QUOTA_EXCEEDED" (Table 5.9.5.3-1).
    - 4.4.5.3.1, MT NIDD downlink data delivery create: "If quota is reached ... 403 Forbidden ...
      QUOTA_EXCEEDED" (Table 5.6.5.3-1, "Not enough quota for the MT NIDD").
    - On these two, a consumer over its share gets 403 `QUOTA_EXCEEDED`. A full store still gets
      500, because the clauses are about the SCS/AS's quota, not the NEF's capacity.
  - Both YAML operations (`CreateNwStatusReportSubscription`, `CreateDownlinkDataDelivery`) list
    403 and 500.
  - TS 29.122 5.12 ECRControl also defines `QUOTA_EXCEEDED`, but its operations are custom
    operations that store nothing, so it is not used there.
  - TS 29.522 j70, TS 29.551 j40, TS 29.591 j70 and TS 29.541 j30 define no store-quota cause.
    The TS 29.551 `INSUFFICIENT_RESOURCES` in PfdChangeReport is a report the consumer sends, not a
    response cause.
- **Ordering.** Every create site calls the store before any call to the PCF, UDM, UDR, TSCTSF or
  AAnF, which was checked by script over all 66 sites. A refused create therefore leaves no
  app-session or EE subscription behind at a peer.

**Disclosed.**

- **The bound applies per store.** The NEF can hold up to 64 × `store_max_entries` entries (65
  stores less the seed-only PFD catalog).
- **Shared stores share one bound.**
  - `af_nidd` holds NIDD configurations, downlink data deliveries and rds-ports.
  - `af_gmd_mb2`, `af_gmd_xmb`, `af_time_sync` and `af_mbs_session` each hold two or three
    resource types.
  - Their per-SCS/AS share is therefore across all the types in the store. For MT NIDD,
    `QUOTA_EXCEEDED` can follow from configurations the SCS/AS holds, not only from buffered data.
- **The consumer is the token's `sub`.** In this lab that is the NRF-issued token (user decision
  2026-10-17, ADR-0499), not a CAPIF TS 33.122 token. An AF whose token names a different `sub` per
  request is a different consumer each time; only the store bound limits it. The `scsAsId` in the
  path is written by the client and is not used.
- **The SCS/AS "rate of submitting requests" (429)** in the same clauses is not built here. The
  NF-wide TPS ceiling (ADR-0280) is the only rate control.
- **No expiry here.** This ADR bounds the stores; the expiries the APIs make mandatory are
  ADR-0538. The UDR is still open under SUS-02.
- **`--next_id_` on refusal.** A refused create gives back the id it drew, so ids stay dense. This
  has no protocol meaning.

**Rejected alternatives.**

- **403 `QUOTA_EXCEEDED` on every northbound API.** TS 29.122 defines it only in the NIDD,
  ReportingNetworkStatus and ECRControl APIs. Sending it elsewhere would use a cause outside the API
  that defines it.
- **The path's `scsAsId` / `afId` as the consumer key.** The client writes it and can claim any
  value. The verified token `sub` is set by the NRF (as in ADR-0535).
- **One bound across all NEF stores.** One busy API could then starve every other API. The UDM
  precedent is per store.
- **Evicting the oldest entry.** That would drop another AF's subscription without telling it.

**Verified (2026-10-30).**

- **Spec texts read:** TS 29.122 j70 (4.4.5.3.1, 4.4.8.2, 4.4.11, 5.2.6, 5.6.5.3, 5.9.5.3,
  5.12.5.3), TS 29.522 j70, and TS 29.551 j40, TS 29.591 j70 and TS 29.541 j30 (fetched with
  `tools/specs/fetch_3gpp_specs.py`). YAML: `TS29122_ReportingNetworkStatus.yaml` and
  `TS29122_NIDD.yaml` 403/500.
- **Syntax:** `g++-15 -fsyntax-only` with the build's own flags on `nfs/nef/src/main.cpp`,
  `nfs/nef/src/stores.cpp`, `nfs/udm/src/stores.cpp`, `tests/conformance/test_sbi_store_quota.cpp`
  and `tests/integration/test_nef_store_bounds.cpp`. No errors. The one warning is the
  `TlsConfig` initializer pattern that every integration test's `make_client()` has.
- **Formatting:** `clang-format-18` applied.
- **Generated files:** `gen_compose_config.py --check`, `gen_helm_config.py --check` and
  `derive_nf_config_schemas.py --check` pass.
- **Tests added but not run here.** A build was already running on this host, which the CI runner
  shares, so no build or ctest was started:
  - `SbiStoreQuota.ARefusalSaysWhetherTheStoreOrTheConsumerIsFull`
  - `NefStoreBounds.*`: 403 `QUOTA_EXCEEDED` and the release on DELETE; 500 on TimeSync for a
    consumer over its share; 500 on a full store where `QUOTA_EXCEEDED` is defined.
