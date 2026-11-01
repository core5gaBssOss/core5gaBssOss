## ADR-0541: UAS (drones): Naf_Authentication (TS 29.255) and the UAE server (TS 29.257)

**Date:** 2026-11-01. **Status:** in progress -- scope recorded and README/diagram/status placeholders landed; no code yet. Each implementation PR moves this on.

**Context.** The NEF already serves `Nnef_Authentication` (TS29256) and UAV flight assistance (TS29522_UAVFlightAssistance). Missing: the USS-facing `Naf_Authentication` (TS29255, 1 file) and the UAE server (TS29257, 8 files: C2 operation mode, USS change, DAA support, flight path monitoring, flight route support, NTZ management, real-time UAV status, and UAV dynamic information). Also missing are UAV-related AMF events (TS29518_Namf_EventExposure) and UDM subscription data (TS29503_Nudm_SDM). Stage 2 for UAS connectivity is TS 23.256.

**Decision:**

- First a USS test double that serves `Naf_Authentication` (TS 29.255), so the NEF's UUAA path can be tested end to end.
- Then the UAE server as a new NF binary, `nfs/uae/`, built against TS29257, one service group per increment.
- AMF and UDM UAV fields as separate small increments, with their YAML schema names quoted in each PR.

**Disclosed now.** Nothing in this track is implemented. The README, `docs/COMPONENT_STATUS.md` and the architecture diagram show it as a dashed placeholder (🕒 Already in Pipeline). The stage-2 specs are fetched with `tools/specs/fetch_3gpp_specs.py` when the track starts, and any stage-3 API not yet published is recorded in the README as deferred for future implementation, never filled by invention.

**Rejected alternatives.**

- *Folding the UAE server into the NEF.* Rejected: TS29257 defines it as its own application-layer server; NFs talk only over SBI.
- *Hand-writing DTOs or paths ahead of the YAML.* Rejected: CLAUDE.md source-of-truth rule; every type is generated from `specs/5G_APIs-REL-19/`.
- *Building all of it in one change.* Rejected: one NF or subsystem per turn, procedure list approved first (guardrail 2).
