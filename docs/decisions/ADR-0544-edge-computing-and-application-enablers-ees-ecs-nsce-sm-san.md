## ADR-0544: Edge computing and application enablers: EES/ECS, NSCE, SM/SAn, AIMLE, LMS, ADAE, SEALDD, VFL, CAPIF

**Date:** 2026-11-01. **Status:** in progress -- scope recorded and README/diagram/status placeholders landed; no code yet. Each implementation PR moves this on.

**Context.** 140 unused files, the largest track:
- Edge enablement: TS24558, TS29558.
- NSCE: TS29435, TS24549.
- Spatial maps and spatial anchors: TS29437, TS24550.
- AIMLE: TS29482, TS24560.
- LMS: TS24283.
- ADAE and SEAL: TS24559, TS29549.
- SEALDD: TS29548.
- VFL: TS29530.
- CAPIF: TS29222.

COMPONENT_STATUS already lists SEAL, SEALDD, ADAES and CAPIF (rows 43-46).

**Decision:**

- CAPIF first: the other enabler servers publish and expose their APIs through it.
- Then EES/ECS, then the other enablers, each as its own NF binary under `nfs/`.
- The TS 24.xxx files are the client-facing APIs of the same enablers. They are built with their server, not on their own.

**Disclosed now.** Nothing in this track is implemented. The README, `docs/COMPONENT_STATUS.md` and the architecture diagram show it as a dashed placeholder (🕒 Already in Pipeline). The stage-2 specs are fetched with `tools/specs/fetch_3gpp_specs.py` when the track starts, and any stage-3 API not yet published is recorded in the README as deferred for future implementation, never filled by invention.

**Rejected alternatives.**

- *One enabler mega-binary.* Rejected: every NF is an independent binary.
- *Hand-writing DTOs or paths ahead of the YAML.* Rejected: CLAUDE.md source-of-truth rule; every type is generated from `specs/5G_APIs-REL-19/`.
- *Building all of it in one change.* Rejected: one NF or subsystem per turn, procedure list approved first (guardrail 2).
