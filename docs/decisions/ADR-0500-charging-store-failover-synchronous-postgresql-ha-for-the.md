## ADR-0500: Charging store failover: synchronous PostgreSQL HA for the authoritative balance; CHF Valkey state as a rebuildable projection

**Date:** 2026-10-18. **Status:** Proposed. Nothing in this ADR is implemented; it is the plan for
ADR-0445's increment 4 (HA), written in answer to a review question: "is Valkey the authoritative
balance store, and how are you ensuring durability and consistency during cluster failover?"

**Context -- what is true today (each point checked in the tree on 2026-10-18).**

1. **PostgreSQL is the authoritative balance store; Valkey is not.** ADR-0445 Option 1. Reserve,
   unreserve and debit are single conditional `UPDATE`s in one transaction
   (`bss/balance-management/src/store.cpp:510-568`; e.g. reserve is
   `... WHERE id = $2 AND remaining_value_amount >= $1`), serialized by the row lock. No Lua script,
   Valkey Function or `MULTI` is involved in money movement.
2. **The CHF's Valkey holds per-session state only**: the reserved/granted running totals per
   `ChargingDataRef` (`HINCRBYFLOAT`, `nfs/chf/src/stores.hpp:134-160`), idempotency records,
   IDs (`INCR`). Every CHF operation is single-key (ADR-0444).
3. **There is no PostgreSQL HA.** `deploy/docker/docker-compose.yml` runs one `postgres-chf`
   (charging + balance + catalog); no streaming replica, Patroni, repmgr or CloudNativePG anywhere
   in `deploy/` (ADR-0445 finding #8, still true). A committed reserve survives a process crash on
   that node (WAL), not the loss of the node.
4. **The CHF's Valkey is a single node with no durability.** CHF uses `valkey:6379`,
   `redis_mode: single` (`config/chf.json`); the `valkey` service has no `command` (so no
   `--appendonly`), no replica and no data volume. The 6-node Valkey Cluster with AOF (ADR-0451) is
   used by the UDSF only; the CHF's store client cannot follow MOVED/ASK redirects (compose comment
   at the cluster block).
5. **Losing the CHF's Valkey strands money.** ADR-0445 Decision B: the session's reserved total
   lives only in Valkey. ADR-0447 added the `reserve_balance.charging_data_ref` correlator
   (`deploy/db/charging/40-balance.sql:102`), but nothing yet *reads* it: the CHF never rebuilds a
   missing total from PostgreSQL, and the reconciliation sweep ADR-0447 scopes is not built
   (`nfs/chf/src/main.cpp:1113`: "No reconciliation sweep exists yet"). If the key is gone at
   Release, `get_reserved_total()` returns 0 and the reservation stays in
   `bucket.reserved_value_amount`.
6. **A reserve is not idempotent across an ambiguous commit.** `reserve_balance` has no
   invocation-sequence column and no unique key on (`charging_data_ref`, sequence)
   (`40-balance.sql:98-111`). After any failover, a client that lost the commit acknowledgement
   cannot tell "committed" from "not committed"; retrying would reserve twice, not retrying may
   under-reserve.

**Decision (proposed).**

*A. PostgreSQL -- the authoritative store gets synchronous replication and managed failover.*
- One synchronous standby per primary (`synchronous_standby_names`, `synchronous_commit = on`), so
  a reserve the client saw committed exists on two nodes before the client is told. Asynchronous
  replication is rejected for money (below).
- Managed failover with fencing: Patroni in Compose (its DCS leader lock decides the single writer
  and demotes a returning old primary); CloudNativePG for the Helm deployment. Clients reach the
  writer through one endpoint (Patroni REST health behind HAProxy in Compose; the operator's `-rw`
  Service in Kubernetes), never a node address.
- Make every balance mutation retry-safe: add an internal-only `invocation_seq` column (not a TMF654
  wire field, the same pattern as the ADR-0447 correlator) and a unique key on
  (`charging_data_ref`, `invocation_seq`) to `reserve_balance` and `adjust_balance`. A retry after a
  lost acknowledgement then either inserts once or hits the key and returns the original outcome.
  `libs/nf-config` `PgPool` retries a transaction only through this path.

*B. CHF Valkey -- a rebuildable projection, not a record.*
- Build ADR-0445 Decision B's read side: when a session's total is missing from Valkey, the CHF
  rebuilds it from `SUM(amount_value) FROM reserve_balance WHERE charging_data_ref = $1` (through
  balance-management's API, preserving the ODA boundary) instead of using 0.
- Build ADR-0447's reconciliation sweep (off by default, internal) for reservations whose session
  ended without a Release.
- With both in place, losing Valkey costs a rebuild, not money, and Valkey needs only ordinary
  durability: AOF (`appendfsync everysec`) and a data volume on the CHF's instance, plus a replica
  via Sentinel or a move to the cluster (that move first needs a cluster-capable CHF store client).
  Per-write `WAIT` / `appendfsync always` is not adopted (below).

*C. Evidence before any claim.* No RPO/RTO figure is stated until measured (ADR-0049, ADR-0445's
evidence plan). Tests to write, each derived from a failure, not a happy path:
- kill the PostgreSQL primary during concurrent reserves on one bucket: afterwards
  remaining + reserved + debited equals the starting balance, every reserve the client saw
  committed is present, none is applied twice;
- kill the CHF's Valkey between Create and Release: Release settles the amount PostgreSQL recorded;
- a retried reserve with the same (`charging_data_ref`, `invocation_seq`) moves money once.

**Rejected alternatives.**
- **Valkey as the authoritative balance (Lua scripts / Valkey Functions as the serializer).**
  ADR-0445 Option 3, already rejected there: it moves atomicity onto Valkey's scripting and needs a
  durable journal and fencing that PostgreSQL already provides. Asynchronous Valkey replication
  can lose acknowledged writes on failover, which is not acceptable for money.
- **Asynchronous PostgreSQL replication.** Lower write latency, but a failover can drop reserves
  the client was told committed (RPO > 0 for money).
- **Multi-primary PostgreSQL (BDR/active-active).** Concurrent updates of one bucket row on two
  primaries conflict, and resolving them breaks the single-row atomicity the design relies on.
- **Making Valkey strongly consistent per write (`WAIT n`, `appendfsync always`).** Hot-path
  latency on every update, and `WAIT` still does not make Valkey replication strongly consistent
  through a failover. Unnecessary once Valkey state is rebuildable from PostgreSQL (B).
- **Dual write of the balance to PostgreSQL and Valkey.** Already rejected in
  `bss/balance-management/schema.sql`'s header: the two stores drift on a crash between writes.

**Open questions for the user (stop-and-ask; not decided here).**
1. When the synchronous standby is down, should PostgreSQL block balance writes (Patroni
   `synchronous_mode_strict`: no charging, no risk) or fall back to asynchronous (charging
   continues, a failover in that window can lose acknowledged reserves)? This is a business
   choice; it interacts with ADR-0445's open TS 32.291 `FailureHandling` decision.
2. Patroni (Compose) plus CloudNativePG (Helm) as proposed, or one of them for both?
3. Order: the idempotency key and Valkey rebuild (A3, B) first -- they close real money-loss
   windows that exist even without HA -- then replication and failover (A1-A2)? Recommended.

**Disclosed limits.** Cross-region/geo-redundancy stays with P11 (ADR-0284). This ADR does not
cover the other single-instance PostgreSQL stores (`postgres-udr`, `postgres-adrf`, LI) or the
non-CHF Valkey users, although the same gaps apply to them.
