## ADR-0543: Ambient IoT completion: AIOTF (TS 29.569), Nadm_Sec (TS 29.369), NRF/NSSF/CHF AIoT fields

**Date:** 2026-11-01. **Status:** in progress -- scope recorded and README/diagram/status placeholders landed; no code yet. Each implementation PR moves this on.

**Context.** Ambient IoT is partly in: the AMF serves `Namf_AIoT` (TS29518), the NEF serves AIoT exposure (TS29522_AIoT, ADR-0327), and UDR AIoT data (TS29506_Aiot_Data, ADR-0256) plus `Nadm_DM` (TS29369) are in. Still missing:
- The AIOTF itself: `Naiotf_AIoT` (TS29569, "AIOTF Ambient IoT Service"). Stage 2 is TS 23.369. This is COMPONENT_STATUS row 34.
- `Nadm_Sec` (TS29369).
- The AIoT fields in referenced files: the NRF's AiotfInfo and the namf-aiot / 3gpp-aiot values (TS29510), AIoT slice-selection inputs (TS29531), and AIoT charging data (TS32291).

**Decision:**

- First the NRF, then NSSF fields, so that an AIOTF can register and be discovered. Then the AIOTF NF (`nfs/aiotf/`, TS29569). Then `Nadm_Sec`. Then CHF AIoT charging.
- Each field is quoted from its YAML schema in the PR that adds it.

**Disclosed now.** Nothing in this track is implemented. The README, `docs/COMPONENT_STATUS.md` and the architecture diagram show it as a dashed placeholder (🕒 Already in Pipeline). The stage-2 specs are fetched with `tools/specs/fetch_3gpp_specs.py` when the track starts, and a stage-3 gap is marked in the README's 3GPP spec gaps section, never filled by invention.

**Rejected alternatives.**

- *An AIOTF stub that answers 200 without the TS29569 procedures.* Rejected: it would claim a conformance it does not have.
- *Hand-writing DTOs or paths ahead of the YAML.* Rejected: CLAUDE.md source-of-truth rule; every type is generated from `specs/5G_APIs-REL-19/`.
- *Building all of it in one change.* Rejected: one NF or subsystem per turn, procedure list approved first (guardrail 2).
