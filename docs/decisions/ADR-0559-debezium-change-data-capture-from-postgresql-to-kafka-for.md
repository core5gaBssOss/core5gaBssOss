## ADR-0559: Debezium change data capture from PostgreSQL to Kafka for BSS and analytics consumers only

**Date:** 2026-11-03. **Status:** Proposed.

**Origin.** From the reference-diagram review (data layer: Debezium). Implemented after
ADR-0556 to ADR-0558. See ADR-0556 "Origin".

**Context.** BSS read models and analytics need to see changes in the BSS PostgreSQL databases
(product catalog, orders, provisioning saga state). Today they would have to poll or have each
service publish its own events.

**Decision:**
1. **Debezium** (Apache-2.0), running in Kafka Connect, streams row changes from the BSS-owned
   PostgreSQL databases to Kafka using PostgreSQL logical decoding (`pgoutput`).
2. Every CDC topic's schema is registered under ADR-0556. These schemas come from the table
   DDL and Debezium's envelope, so they are project-owned contracts, not 3GPP or TMF shapes, and
   are labelled that way.
3. Consumers are read-side only: BSS read models, Doris analytics, training sidecars.
4. Not used for:
   - **UDR data-change propagation to NFs.** That stays the 3GPP Nudr subscription and
     notification path (TS 29.504), which the NFs rely on.
   - **Charging decisions.** Balance and rating stay with the CHF and the ABMF's atomic
     operations (ADR-0056); a CDC event never debits, credits or authorises.
   - **TMF event notifications to external parties.** Those stay the TMF `hub`/listener
     notifications each API defines.

**Rejected alternatives.**
- Each BSS service publishes its own events (outbox pattern by hand): more code per service,
  and easy to miss a write path.
- Polling the databases: load on the primary and delay.
- CDC on the UDR for NF consumption: duplicates and bypasses a 3GPP-defined interface.

**Disclosed.**
- Design only. Logical replication slots need `wal_level=logical` and slot monitoring; a slot
  left unconsumed holds WAL on the primary. Operating that is part of the implementation PR.
- Lag and throughput: not measured.
