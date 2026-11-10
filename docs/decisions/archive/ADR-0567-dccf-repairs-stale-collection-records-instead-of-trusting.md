## ADR-0567: DCCF repairs stale collection records instead of trusting them

**Date:** 2026-11-07. **Status:** Closed (work pushed; last citing commit 951efbbf on origin/main, 2026-11-10). Proposed.

**Context.** The DCCF's dedup (TS 23.288 5A.2, ADR-0366) keeps one `dccf:src:<fingerprint>` record
per collection in Valkey, with no TTL. A join trusted the record unchecked. Three stale cases follow
from a peer restart:

1. The MFAF lost the configuration. The join's PUT answered 404, and every later join with that
   fingerprint got a 500 with the record kept.
2. The NSACF lost the source subscription, while the MFAF configuration survived. The PUT succeeded,
   and the consumer "joined" a collection that delivered nothing. The NSACF keeps subscriptions in
   memory and numbers them `sac-sub-1..` again after a restart, so the stale id can also name
   another subscriber's subscription, which the last consumer's teardown then DELETEd.
3. Consumers of a killed client never unsubscribe and stay in the MFAF fan-out.

The Dccf integration test failed locally 3 of 3 runs against a long-lived Valkey holding such
records, and passed 2 of 2 on a fresh one (2026-11-07).

**Decision.**

- **NSACF liveness check.** Before a join trusts an NSACF collection, and before its teardown
  DELETEs the source subscription, the DCCF sends `PATCH {sourceResourceUri}` with
  `application/json-patch+json`. The body holds only RFC 6902 `test` operations: `/eventNotifyUri`
  equals the collection's `mfafNotifUri`, and `/notifyCorrelationId` equals its `mfafCorreId`
  (TS29536_Nnsacf_SliceEventExposure.yaml PATCH, a PatchItem array with `minItems: 1`;
  TS29571_CommonData.yaml PatchOperation includes `test`). The answers mean:
  - 200: alive.
  - 404: gone.
  - 400: gone, because a failed test means the id now names another subscriber's subscription.
  - Anything else: unknown, and the DCCF proceeds as before.
- **Join, MFAF PUT answers 404.** The collection is rebuilt for every consumer it had: a new MFAF
  configuration, a new source subscription, then a PUT with all the carried consumers. The old
  source subscription is deleted only when its id is known to still be ours (NSACF probed alive,
  or the NWDAF, whose subscriptions persist). An AMF or NRF id that was never probed is left in
  place, aimed at a dead MFAF target, rather than risk deleting another subscriber's.
- **Join, PUT succeeds but the NSACF subscription is gone.** The source is subscribed again with
  the same MFAF target, and the configuration is reused. If the source refuses, the joining
  consumer gets 400 `SUBSCRIPTION_CANNOT_BE_SERVED`, the fan-out is restored without it, and the
  record keeps the dead URI so a later join retries.
- **Teardown.** If the probe says gone, no DELETE is sent.
- **Test isolation.** Every data subscription in `tests/integration/test_dccf.cpp` carries a
  run-unique field that changes the fingerprint and that the source does not act on: NSACF
  `expiry`, NRF `reqNfFqdn`. So no earlier run's record is joined.

**Rejected alternatives.**

- **Plain TTL on `dccf:src:`.** A live collection's record would expire. The next subscriber would
  open a duplicate source subscription, and the last consumer's leave would find no record and
  orphan the first one.
- **Refreshed lease (background renewal).** Adds a thread. It does not detect a source restart
  while the DCCF lives, and a rerun within the lease still joins the stale record.
- **Make-before-break re-subscribe on every join.** Doubles source subscriptions, and the DELETE of
  the old one can hit a reused id.
- **Empty JSON Patch as the probe.** The YAML's `minItems: 1` forbids it, and
  `apply_json_patch_body` refuses it.
- **Same-content PUT or replace as the probe.** It is a write, and resets state at the source. For
  the NWDAF it also emits LI events.
- **NFProfile `recoveryTime`.** No NF here sets or reads it. It is an option for future
  implementation, not verified.

**Disclosed, not hidden.**

- **AMF:** liveness is deferred for future implementation. TS29518_Namf_EventExposure.yaml's PATCH
  allows only add, remove and replace, so there is no side-effect-free check.
- **NRF:** liveness is deferred until the NRF applies RFC 6902 PATCH as TS29510_Nnrf_NFManagement.yaml
  requires. Today `SubscriptionRegistry::update` overwrites the whole record, a pre-existing
  non-conformance to be fixed separately. The NRF also answers 204 to a DELETE of an absent
  subscription. So a dead AMF or NRF source subscription is not detected, and after the source
  restarts its id can still be deleted at teardown.
- **NWDAF:** not probed. Its subscriptions survive a restart, and its PUT emits LI events.
- **The NSACF probe is a PATCH.** The NSACF rebuilds its record, so `remain_reports` of a
  `maxReports` subscription is reset by every join or teardown that probes.
- **Each NSACF join adds one request.**
- **Stale consumers (case 3) are not removed.** TS 29.574 gives a consumer subscription no expiry.
- **Records are never garbage-collected.** A record whose fingerprint nobody subscribes to again
  stays. The concurrent same-fingerprint get/put race on the record is pre-existing.
- **Connection reuse after a peer restart** relies on libcurl's dead-connection detection. Nothing in
  the repo retries, and only the integration test exercises it.

**Verified.** CI run 42364767780 (2026-11-07, this change on the pre-rewrite main) built the
code, and its build job passed all 1230 ctest tests. Two of them are
`Dccf.RecoversACollectionWhoseSourceOrMfafRecordIsStale` (24.12 s) and
`Dccf.CoordinatesCollectionThroughTheMfafDedupsAndTearsDown` (20.11 s). `sanitize (asan-ubsan)`
passed. `sanitize (tsan)` was cancelled when the run was superseded after main's history rewrite
(ADR-0566). The code is unchanged in #94, whose CI repeats the run. A failure of the test without
the fix was not observed; it is reasoned from the code.
