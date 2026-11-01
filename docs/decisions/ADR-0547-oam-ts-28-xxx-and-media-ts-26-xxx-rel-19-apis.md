## ADR-0547: OAM (TS 28.xxx) and media (TS 26.xxx) REL-19 APIs

**Date:** 2026-11-01. **Status:** in progress -- scope recorded and README/diagram/status placeholders landed; no code yet. Each implementation PR moves this on.

**Context.** 84 unused files:
- OAM: TS28104 (MDA), TS28105 (AI/ML NRM), TS28111 (fault management), TS28310 (energy), TS28312 (intent), TS28317, TS28318, TS28319, TS28531 (slice provisioning), TS28532 (provisioning, file, streaming, heartbeat and threshold MnS), TS28536, TS28538 (edge NRM), TS28541 (5GC/NR/slice NRM), TS28550 (PM job control), TS28561, TS28567, TS28572, TS28623 (generic NRM).
- Media: TS26113, TS26264, TS26510, TS26512 (5GMS), TS26517, TS26532 (data collection).

**Decision:**

- OAM: one management-service (MnS) producer over the existing NFs. The order is TS28532 provisioning, TS28623/28541 NRM, then TS28550 PM and TS28111 fault, then the rest.
- Media: a 5GMS AF/AS pair (TS26512/26510), then data collection (TS26532), then the rest.
- These go last: they manage or ride on the core rather than complete it.

**Disclosed now.** Nothing in this track is implemented. The README, `docs/COMPONENT_STATUS.md` and the architecture diagram show it as a dashed placeholder (🕒 Already in Pipeline). The stage-2 specs are fetched with `tools/specs/fetch_3gpp_specs.py` when the track starts, and any stage-3 API not yet published is recorded in the README as deferred for future implementation, never filled by invention.

**Rejected alternatives.**

- *Treating OAM as out of scope.* Rejected: the user asked for nothing from R19 to be missed, and the YAML is in hand.
- *Hand-writing DTOs or paths ahead of the YAML.* Rejected: CLAUDE.md source-of-truth rule; every type is generated from `specs/5G_APIs-REL-19/`.
- *Building all of it in one change.* Rejected: one NF or subsystem per turn, procedure list approved first (guardrail 2).
