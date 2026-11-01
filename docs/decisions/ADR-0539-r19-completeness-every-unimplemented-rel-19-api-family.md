## ADR-0539: R19 completeness: every unimplemented REL-19 API family enters the pipeline (umbrella)

**Date:** 2026-11-01. **Status:** in progress -- scope recorded and README/diagram/status placeholders landed; no code yet. Each implementation PR moves this on.

**Context.** The project targets all of REL-19, but `scripts/docs/gen_r19_gap_register.py` (added here) finds that **305 of the 472** REL-19 OpenAPI files in `specs/5G_APIs-REL-19/` are used by no NF, BSS or library code. One of them, `TS29532_Nmbsmf_TMGI`, is named only by a test and the codegen catalog. The user asked to add all of it, starting with V2X and drones, with nothing from R19 missed. A file being referenced does not mean it is fully implemented: V2X/A2X, UAV and Ambient IoT parts inside referenced files (TS29525, TS29518, TS29510, TS29531, TS32291) are also missing.

| Track | ADR | Unused files |
|---|---|---|
| V2X/A2X in the core NFs (inside referenced files) | ADR-0540 | 0 (partial areas) |
| UAS (drones) | ADR-0541 | 9 |
| VAE | ADR-0542 | 12 |
| Ambient IoT completion | ADR-0543 | 2 |
| Edge computing and application enablers | ADR-0544 | 140 |
| IMS, HSS, GBA and messaging | ADR-0545 | 30 |
| ProSe, 5MBS, PIN and remaining 5GC services | ADR-0546 | 28 |
| OAM (TS 28) and media (TS 26) | ADR-0547 | 84 |

**Decision:**

- `docs/R19_COMPLETENESS.md` is the register of every unused REL-19 file, with its owning ADR. It is generated and has a `--check` mode, so it cannot drift by hand. The generator fails on an unmapped spec family, so a new REL-19 file cannot drop out silently.
- Order, as agreed with the user: V2X/A2X core (PCF, then AMF, NRF, CHF; ADR-0540), then USS `Naf_Authentication` (TS 29.255), the UAE server (TS 29.257), and VAE (TS 29.486). Then Ambient IoT, then the other tracks.
- Each track is one subsystem per turn, with its TS 23.502 / stage-2 procedure list shown for approval before any code (guardrail 2).
- Docs first: the README (append/update only), `docs/COMPONENT_STATUS.md` (rows 62+), the architecture diagram (a dashed "R19 verticals, IoT & enablers" area) and the hero image carry every track as a placeholder before code starts.

**Disclosed now.** Nothing in this track is implemented. The README, `docs/COMPONENT_STATUS.md` and the architecture diagram show it as a dashed placeholder (🕒 Already in Pipeline). The stage-2 specs are fetched with `tools/specs/fetch_3gpp_specs.py` when the track starts, and a stage-3 gap is marked in the README's 3GPP spec gaps section, never filled by invention.

**Rejected alternatives.**

- *Leaving the scope at the CLAUDE.md Tier 2/3 lists.* Rejected: those lists miss V2X/VAE, UAS, NSCE, AIMLE, the OAM NRMs and the media APIs, all of which have REL-19 YAML.
- *A hand-kept gap list.* Rejected: it would go stale; the register is generated.
- *Hand-writing DTOs or paths ahead of the YAML.* Rejected: CLAUDE.md source-of-truth rule; every type is generated from `specs/5G_APIs-REL-19/`.
- *Building all of it in one change.* Rejected: one NF or subsystem per turn, procedure list approved first (guardrail 2).
