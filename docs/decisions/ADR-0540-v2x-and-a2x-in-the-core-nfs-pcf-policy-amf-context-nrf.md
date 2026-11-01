## ADR-0540: V2X and A2X in the core NFs: PCF policy, AMF context, NRF fields, CHF charging

**Date:** 2026-11-01. **Status:** in progress -- scope recorded and README/diagram/status placeholders landed; no code yet. Each implementation PR moves this on.

**Context.** V2X and A2X data are already in UDM/UDR (TS29503, TS29504, TS29505). But the parts the core NFs own are missing: the PCF's V2X/A2X policy in `Npcf_UEPolicyControl` (TS29525), the AMF's V2X/A2X context in `Namf_Communication` (TS29518), the NRF's V2X/A2X capability fields (TS29510; for example the PCF's supported V2X capability), and V2X/A2X charging data in the CHF (TS32291). Stage 2 for V2X in 5GS is TS 23.287.

**Decision:**

- Four increments, in this order: PCF, AMF, NRF, CHF. Each one shows its procedure list first.
- Every field comes from the YAML in hand. Where stage 3 is missing, the gap goes into the README spec-gaps section.
- No PC5 radio or UE-side behaviour: that is out of the core's scope and stays so.

**Disclosed now.** Nothing in this track is implemented. The README, `docs/COMPONENT_STATUS.md` and the architecture diagram show it as a dashed placeholder (🕒 Already in Pipeline). The stage-2 specs are fetched with `tools/specs/fetch_3gpp_specs.py` when the track starts, and a stage-3 gap is marked in the README's 3GPP spec gaps section, never filled by invention.

**Rejected alternatives.**

- *Starting with the VAE server.* Rejected: the VAE needs the core's V2X policy and exposure path underneath it.
- *Hand-writing DTOs or paths ahead of the YAML.* Rejected: CLAUDE.md source-of-truth rule; every type is generated from `specs/5G_APIs-REL-19/`.
- *Building all of it in one change.* Rejected: one NF or subsystem per turn, procedure list approved first (guardrail 2).
