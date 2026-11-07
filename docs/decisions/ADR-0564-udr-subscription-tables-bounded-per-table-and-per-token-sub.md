## ADR-0564: UDR subscription tables bounded per table and per token sub; confirmed subscription expiry (SUS-02)

**Date:** 2026-11-06. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SUS-02. After the UDM (ADR-0535) and the NEF (ADR-0536, ADR-0538),
the review's status table leaves the UDR open. Unlike those two NFs, all 82 UDR stores are
PostgreSQL tables (ADR-0068), not in-memory maps. Most of them hold per-subscriber data that the UDM
and OAM provisioning write for the whole subscriber base.

Two things grow without a bound:

- **The UDR's own notification subscriptions.** There are six collections: `subscription-data`,
  `policy-data`, `application-data`, `application-data/influenceData` and `exposure-data`
  `subs-to-notify`, plus `nf-group-ids/subscriptions`. All six never expire, although each schema
  carries an `expiry`.
- **The UDM's persisted subscription copies.** These are `context-data/ee-subscriptions`,
  `sdm-subscriptions`, `group-data/.../ee-subscriptions` and their seven `*-subscriptions`
  subscription-info documents.

User decisions (2026-11-06):

- Bound the subscriptions.
- Refuse writes for unprovisioned users. That is a separate increment, because it changes behaviour
  well beyond this finding.
- Answer a consumer over its share with 403 `CREATION_NOT_ALLOWED`, and a full table with 500
  `INSUFFICIENT_RESOURCES`.
- Default limits: 1,000,000 per table and per consumer, 24 h maximum lifetime, 60 s sweep. A
  stateful UDM holds one UDR subscription per UE it serves, so the per-consumer default is inert
  and operators can lower it.

**Decision:** the UDR's 16 subscription tables are bounded per table and per access-token `sub`,
by counts a trigger keeps in the database. The six UDR-owned subscription collections confirm an
expiry and are deleted once it passes.

- **Counts in the database.**
  - Each bounded table gets a `consumer` column holding the creating request's token `sub`
    (`http2::Request::token_sub`; `''` without a token).
  - `udr_store_quota_count()`, an `AFTER INSERT OR DELETE` row trigger, keeps
    `udr_store_quota(store, consumer, entries)` in step, with `'*'` as a table's total. Every delete
    path updates the counts, the sweep and manual deletes included, and the counts survive a
    restart.
  - Applying `schema.postgres.sql` recounts each table, so rows written before the trigger existed
    are counted too.
  - A store reads two count rows (`udr::quota_refusal`) before inserting a new row. Replacing an
    existing row (`ON CONFLICT DO UPDATE`) is never refused and keeps its creator.
  - `sbi_core::StoreQuota` (ADR-0535) was not reused: it lives in memory, and these rows persist.
- **Refusal.**
  - A consumer over its share gets 403 `CREATION_NOT_ALLOWED`. TS 29.504 table 6.1.6-2 defines it
    as "Creation of the resource is not allowed", and TS 29.519 tables 5.5.3-1, 6.5.3-1 and 7.4.3-1
    are empty and defer to TS 29.504 clause 6.1.6.
  - A full table gets 500 `INSUFFICIENT_RESOURCES` (TS 29.500 table 5.2.7.2-1).
  - Nothing is stored in either case. Every refused route lists 403 and 500 in its YAML.
- **Expiry of the six UDR-owned collections.** The attributes are:
  - `SubscriptionDataSubscriptions.expiry` (TS29505);
  - `PolicyDataSubscription.expiry`, `TrafficInfluSub.expiry`, `ApplicationDataSubs.expiry` and
    `ExposureDataSubscription.expiry` (TS29519);
  - `SubscriptionData.expiry` (TS29504_Nudr_GroupIDmap).

  TS 29.504 5.2.2.6.2 (and 5.2.2.6.3 and 5.3.2.4.2) says the response "may contain a confirmed
  expiry time ..., as determined by the UDR, after which the subscription becomes invalid". It also
  says "the confirmed expiry time should be less than or equal to the suggested expiry time" and
  "The UDR shall not provide the same expiry time ... for many subscriptions".

  So the UDR applies `sbi_core::confirm_expiry_in`, the ADR-0535 rule moved into sbi-core
  (`subscription_expiry.hpp`) and shared with the UDM:
  - It keeps a proposal no later than now + `subscription_max_lifetime_seconds`.
  - Otherwise, or when nothing is proposed, it sets that maximum less a random spread of up to a
    tenth.
  - A modify (PATCH or PUT) confirms the expiry again.
  - A value that is not an RFC 3339 date-time gets 400 `OPTIONAL_IE_INCORRECT`.

  An `expires_at` column, indexed, holds the confirmed time. `udr::ExpirySweeper`, on its own
  connection, deletes the passed rows every `store_sweep_interval_seconds`.
- **Configuration.** `config/udr.json` gets `store_max_entries`, `store_max_entries_per_consumer`,
  `subscription_max_lifetime_seconds` and `store_sweep_interval_seconds`. Each has a `UDR_*`
  environment override. A non-positive lifetime or interval is a fatal configuration error.

**Disclosed.**

- **The UDM's copies are bounded but not expired.** Their expiry is the UDM's own (TS 29.503), not
  a Nudr attribute.
- **Rows from before this change keep no expiry.** The UDR confirmed them without an expiry, which
  TS 29.504 5.2.2.6.2 defines as "unlimited in time until explicitly terminated". Deleting them
  would contradict what the consumer was told.
- **The sweep is periodic.** A subscription can outlive its expiry by up to one interval. Until the
  sweep removes it, it is still listed and still notified. Its removal sends no notification, and
  the spec asks for none.
- **The check is not atomic across processes.** It runs before the INSERT under the store's
  in-process lock. Two UDR processes sharing one database could exceed a limit by the creates they
  have in flight; the Helm chart runs one replica.
- **Not covered.** `/data-restoration-events` (`udr_data_restoration_subscriptions`) stays
  unbounded. Its YAML body is `{}`, it has no expiry attribute, and it has no individual resource,
  so a bounded table could never be emptied through the API.
- **Two PUTs still create.** The `application-data` and `influenceData` `subs-to-notify/{id}` PUTs
  upsert, although their YAML lists 200/204 and 404 but no 201 (the same class as SPEC-07). This
  ADR leaves that unchanged, but those creates are now bounded too.
- **Open, next increment:** 404 `USER_NOT_FOUND` (TS 29.504 table 6.1.6-2) for writes under
  `subscription-data/{ueId}` for a ueId the UDR has not provisioned.

**Rejected alternatives.**

- *Bounding every UDR table.* Per-subscriber data grows with provisioning, and a per-consumer limit
  would refuse the UDM's normal writes (user decision).
- *`SELECT count(*)` per create.* It is O(rows) at the 1,000,000 default; the trigger-kept counts
  cost one key lookup.
- *In-memory counts rebuilt at startup.* That means a full table scan per bounded table at every
  start, and the counts drift from anything that deletes outside the UDR.
- *500 for both refusals.* TS 29.504 defines a Nudr cause for a refused creation, and the user
  chose it for the consumer's share, matching the NEF split (ADR-0536).

**Verified (2026-11-06).**

- **Spec texts read:** TS 29.504 j80 and TS 29.519 j80 (`tools/specs/fetch_3gpp_specs.py`, fetched
  to a scratch directory, `MANIFEST.tsv` untouched), and TS 29.500 j71.
- **Schema, on PostgreSQL 16.15:** applied over the previous schema, then again (idempotent). Rows
  that existed beforehand were counted. The insert, upsert, delete and sweep statements the stores
  send kept the counts exact.
- **Syntax:** `g++` 13.3 and `clang++` 18.1.3 (`-std=c++23 -fsyntax-only`, project warning flags)
  on the changed UDR and UDM translation units and the new test. Headers were libpqxx 8.0.2, as
  the vcpkg baseline pins. No errors, and no new warning on a changed line. Locally, clang 18 with
  libstdc++ 13 exposes `std::expected` only with `__cpp_concepts` raised; the unchanged NEF code
  needs the same.
- **Formatting:** `clang-format-18 --dry-run --Werror` exits 0 on every changed C++ file.
- **Tests added, not run here** (there is no build tree): `UdrStoreBounds.*` (five tests).
