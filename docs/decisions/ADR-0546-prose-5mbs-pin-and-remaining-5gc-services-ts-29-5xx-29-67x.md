## ADR-0546: ProSe, 5MBS, PIN and remaining 5GC services (TS 29.5xx/29.67x)

**Date:** 2026-11-01. **Status:** in progress -- scope recorded and README/diagram/status placeholders landed; no code yet. Each implementation PR moves this on.

**Context.** 28 unused files:
- ProSe: TS29555 (5G DDNMF), TS29557 (Naf_ProSe), TS29559 (PKMF), TS29553 (PAnF), TS29586 (SLPKMF).
- SoR: TS29550.
- MNP: TS29578.
- PIN: TS29583.
- 5MBS: TS29532 MBSSession and TMGI (the latter test-only), TS29580 (MBSF), TS29581 (MBSTF).
- EASDF: TS29556.
- UCMF: TS29673, TS29675.
- NSSAAF: TS29526.
- TSCTSF: TS29565 ASTI and TimeSynchronization.
- EIF: TS29566.

**Decision:**

- 5MBS (MB-SMF, MBSF, MBSTF) and the NSSAAF first: they complete core procedures that the existing NFs already signal toward.
- Then the TSCTSF services, to close the partial TSCTSF, then the EASDF and the ProSe family, then the rest.
- Each NF's procedure list comes first, as always.

**Disclosed now.** Nothing in this track is implemented. The README, `docs/COMPONENT_STATUS.md` and the architecture diagram show it as a dashed placeholder (🕒 Already in Pipeline). The stage-2 specs are fetched with `tools/specs/fetch_3gpp_specs.py` when the track starts, and any stage-3 API not yet published is recorded in the README as deferred for future implementation, never filled by invention.

**Rejected alternatives.**

- *Hand-writing DTOs or paths ahead of the YAML.* Rejected: CLAUDE.md source-of-truth rule; every type is generated from `specs/5G_APIs-REL-19/`.
- *Building all of it in one change.* Rejected: one NF or subsystem per turn, procedure list approved first (guardrail 2).
