## ADR-0565: UDR refuses writes under subscription-data/{ueId} for an unprovisioned user

**Date:** 2026-11-06. **Status:** Accepted.

**Context.** REVIEW-5ae0ef1 SUS-02 follow-up. ADR-0564 bounds the UDR's subscription tables and
records the user decision of 2026-11-06 to refuse writes for unprovisioned users as a separate
increment. Until now, any authorized consumer could create rows under
`/subscription-data/{ueId}/...` for a `ueId` no subscriber has. Those rows were per-user data for a
user that does not exist, and only the subscription tables are bounded.

The specifications support the refusal:

- TS 29.504 V19.8.0 table 6.1.6-2 defines `USER_NOT_FOUND`, 404 Not Found: "The user indicated in
  the HTTP/2 request does not exist in the UDR."
- `TS29505_Subscription_Data.yaml` has 62 write operations (PUT, POST, PATCH, DELETE) under
  `/subscription-data/{ueId}/` (measured). All 62 list a `404` response. The UDR registers routes
  for 61 of them, and all 61 go through the guard (measured against `nfs/udr/src/main.cpp`).
- The `ueId` schema of those operations is `Supi` on 10 and `VarUeId` on 50. On the other 2,
  PUT and DELETE `/subscription-data/{ueId}/pp-data-store/{afInstanceId}`, it is
  `anyOf: [VarUeId, ExtGroupId, {pattern: '^anyUE$'}]`.

**Decision:** the UDR answers a write under `/subscription-data/{ueId}/` with 404 `USER_NOT_FOUND`
and stores nothing when `ueId` is not a user it has provisioned.

- **One registrar.** All data routes in `nfs/udr/src/main.cpp` register through
  `udr::ProvisionedWriteRoutes` (`nfs/udr/src/provisioned_guard.hpp`). It wraps every non-GET route
  whose pattern starts with `{apiRoot}/subscription-data/{ueId}/`, so a write route added later is
  guarded by registering it.
- **Order and failure.** The check runs after authorization and before body validation, so an
  unknown user gets the 404 and not a 400 for its body. A database failure throws and the server
  answers 5xx, so a write is never let through on one.
- **What counts as provisioned.** `ProvisionedDataStore::exists` looks across all serving PLMNs:
  - A GPSI (`msisdn-`, `extid-`) is provisioned when a subscriber's `amData.gpsis` lists it. A GIN
    index on `am_data -> 'gpsis'` serves that lookup.
  - Any other id is provisioned when it is the key of a `udr_provisioned_data` row.
- **Non-user ids.** `pp-data-store/{afInstanceId}` alone also accepts `anyUE` and an `ExtGroupId`,
  matching the YAML's `anyOf` on exactly those 2 of the 62 operations. On any other route such an
  id is not a provisioned user and gets the 404.
- **An existing database does not get the index on its own.** The UDR binary does not apply
  `nfs/udr/schema.postgres.sql`. Compose applies it through `docker-entrypoint-initdb.d`, on first
  initialization only; CI runs it with `psql`; the Helm chart says the PostgreSQL schema is external
  (`deploy/helm/udr/Chart.yaml`). A database created earlier needs the schema re-applied by hand
  (`CREATE INDEX IF NOT EXISTS`, re-applied here with no errors). Without the index the GPSI lookup
  is slower but gives the same answer.
- **Reads are unchanged.** A GET keeps its existing behaviour.
- **Onboarding is unchanged.** OAM provisioning (`/oam-provisioning/v1/subscribers/{supi}`,
  ADR-0382) is outside the guarded prefix and remains how a user comes to exist.

**Not guarded, and not conformance claims.**

- PATCH `/subscription-data/{ueId}/context-data/amf-non-3gpp-access` (`AmfContextNon3gpp`) is the
  62nd operation. The UDR has no route for it, so it answers 405 with `Allow` whether or not the
  user is provisioned. ADR-0094 calls this resource "GET+PUT only, no PATCH/DELETE", which does not
  match the YAML: line 1791 defines the PATCH. The missing route predates this ADR and is deferred
  for future implementation; when it is added it goes through the guard like the others.
- Literal routes under `subscription-data/`, such as `subs-to-notify[/{id}]` and `group-data/...`,
  have no `{ueId}`, so they are outside the guard.
- `policy-data`, `exposure-data` and `application-data` are outside it too, so a consumer can still
  write those for an unprovisioned user. They are deferred for future implementation.
- `ueId` is matched as sent. There is no check that a SUPI or GPSI is otherwise well formed beyond
  what each route already does.

**Effect on other NFs.**

- A survey of `nfs/` and `bss/` found only the UDM calling `subscription-data` on the UDR.
- UDM UECM registrations and SDM subscriptions do not write to the UDR, so a registration for an
  unknown user behaves as before.
- Nudm_PP `pp-data-store` PUT and DELETE for an unknown user now return the UDR's 404. `proxy_to_udr`
  passes it through with content type `application/json`, not `application/problem+json`. That
  behaviour predates this ADR and is not changed here.
- SQN write-back (`udr_auth_source.cpp`) for a user removed between its fetch and its PATCH already
  maps to `AuthDataError::Unavailable`.

**Tests.**

- New: `tests/integration/test_udr_unprovisioned_writes.cpp`.
- `tests/integration/udr_test_provisioning.hpp` onboards a test user through
  `provisioning::UdrAdapter`.
- `test_tsctsf_e2e` and `test_udr_ondatachange_webhook` wrote for users they had not provisioned, so
  they now provision them first.

**Verified (2026-11-07, locally; CI has not run this branch).**

- Refusal and acceptance: `UdrUnprovisionedWrites.AnUnprovisionedSupiIsRefusedAcrossTheWriteMethods`,
  `.AWriteForAProvisionedSupiIsAccepted`, `.AGpsiIsProvisionedOnlyWhenASubscriberListsIt` and
  `.OnlyPpDataStoreTakesAGroupOrAnyUe` pass.
- Fails without the fix: with the guard disabled, the refusal, GPSI and `pp-data-store` tests fail
  (for example PUT smf-registration 201, PATCH operator-specific-data 200). The provisioned-user test
  still passes, as a positive control.
- Provisioning-first callers: `tsctsf_e2e_integration_tests` (4 of 4) and
  `UdrIntegration.OnDataChangeWebhookDeliveredOnPutPatchDelete` pass.
- Full `ctest --timeout 120`: 1226 tests, "99% tests passed, 2 tests failed", 1916.46 s, with 4
  skipped. This run also had an unrelated test-only change applied
  (`SmfHandoverCancel.HoStateCancelledIsRecordedAndEchoed`, separate PR). Neither failure is
  attributed to this change:
  - `gui_nf_config_schemas_up_to_date`: the local CMake Python has no PyYAML. Run with a Python that
    has it, the same check prints "37 NF config schemas up to date".
  - `Dccf.CoordinatesCollectionThroughTheMfafDedupsAndTearsDown`: failed 3 of 3 against the local
    long-lived Valkey, which held `dccf:src:*` records from earlier runs, and passed 2 of 2 against a
    fresh Valkey. The test spawns no UDR.
- Skipped because the NWDAF training sidecar is not set up locally: no venv for
  `NwdafMtlf.TrainsStoresProvisionsAndTheAnlfPredicts` and
  `NwdafMtlf.AccuracyBelowThresholdIsReportedAndDrivesRetraining`; numpy not importable for
  `NwdafMlTrainingSub.FlRoundIsReportedOncePerRoundIndAndSkipTrainsNothing` and
  `NwdafFlClient.RealSidecarRoundOnRealSeries`.
- The local database setup differs from CI's: `70-partition-management.sql` not applied (no
  pg_partman), no `orchestration` database, and services on local ports.

**Rejected alternatives.**

- **A check inside each handler.** With 62 write operations, a later route could simply miss it.
- **A catch-all filter on the request path.** The registrar matches the route pattern, which the
  server has already parsed, so there is no second path parser to keep in step.
- **Guarding GET as well.** Out of scope for the decision, which is about writes, and it would change
  responses consumers already depend on.
- **Treating `anyUE` and group ids as allowed on every route.** The YAML allows them only on
  `pp-data-store`.
- **`DATA_NOT_FOUND`.** Table 6.1.6-2 defines it too, but `USER_NOT_FOUND` is the row that describes
  a missing user.
- **Applying the guard to `policy-data`, `exposure-data` and `application-data` in this increment.**
  This scoping is my choice, not part of the user decision, which says only "unprovisioned users".
  Those three trees have other key forms and consumers and need their own survey.
