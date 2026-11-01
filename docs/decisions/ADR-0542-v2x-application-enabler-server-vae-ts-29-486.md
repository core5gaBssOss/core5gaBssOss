## ADR-0542: V2X application enabler server (VAE, TS 29.486)

**Date:** 2026-11-01. **Status:** in progress -- scope recorded and README/diagram/status placeholders landed; no code yet. Each implementation PR moves this on.

**Context.** TS29486 (12 files) defines the VAE server's APIs: application requirement, dynamic group, file distribution, HD map dynamic info, message delivery, PC5 provisioning requirement, service and QoS control info, service continuity, session-oriented service, V2P application requirement, V2V config requirement, and VRU zone management. None of them is implemented. Stage 2 for the V2X application layer is TS 23.286.

**Decision:**

- A new NF binary, `nfs/vae/`. It consumes the NEF and PCF V2X paths from ADR-0540; it does not bypass them.
- Built one service group per increment, starting with the requirement and policy groups that drive the core (application requirement, V2V config, PC5 provisioning).
- The HD-map and file-distribution payloads are carried and stored as the YAML defines them; no map content is generated.

**Disclosed now.** Nothing in this track is implemented. The README, `docs/COMPONENT_STATUS.md` and the architecture diagram show it as a dashed placeholder (🕒 Already in Pipeline). The stage-2 specs are fetched with `tools/specs/fetch_3gpp_specs.py` when the track starts, and a stage-3 gap is marked in the README's 3GPP spec gaps section, never filled by invention.

**Rejected alternatives.**

- *Building the VAE before the core V2X policy (ADR-0540).* Rejected: it would have nothing conformant to call.
- *Hand-writing DTOs or paths ahead of the YAML.* Rejected: CLAUDE.md source-of-truth rule; every type is generated from `specs/5G_APIs-REL-19/`.
- *Building all of it in one change.* Rejected: one NF or subsystem per turn, procedure list approved first (guardrail 2).
