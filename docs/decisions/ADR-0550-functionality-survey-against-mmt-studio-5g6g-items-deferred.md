## ADR-0550: Functionality survey against mmt-studio-5g6g: items deferred for future implementation

**Date:** 2026-11-01. **Status:** in progress -- survey recorded; every item below is deferred for future implementation, no code yet. Each implementation PR moves its row on.

**Context.** The user asked for a comparison of this core against `Makemytechnology/mmt-studio-5g6g` (branch `main`, surveyed 2026-11-01), with every function it has and we lack recorded in an ADR.

- That repository is a Go code base under **AGPL-3.0**. It was compared by **functionality only**. No code, structure or text was copied, and none may be: AGPL is incompatible with this project's licensing, and every API here is generated from the 3GPP YAML (CLAUDE.md source-of-truth rule), not ported.
- Its module depth was judged from the repository tree (file counts per module). Most of its modules are a single Go file and none has tests; the code itself was not read. A module's presence there says the topic exists, not that it is conformant.
- Every row below is mapped onto the 3GPP spec that defines it. TS 23.501 sections were checked against the local `specs/3gpp/TS_23.501_j80` (V19.8.0). TS 23.289 (V19.8.0) and TS 23.379 (V19.11.0) were fetched for this ADR and checked.

**Decision.** Record the following as deferred for future implementation. Where a track ADR from #75 already holds the item, this ADR only cross-references it.

| # | mmt module(s) | Function | Spec (verified unless marked) | Our status |
|---|---|---|---|---|
| 1 | `nf/n3iwf`, `wifi_offload` | Untrusted / trusted non-3GPP access: N3IWF, TNGF, TWIF, W-AGF (wireline), NSWOF, non-seamless WLAN offload | TS 23.501 §4.2.8, §5.5, §4.2.15, §5.42; §6.2.9, §6.2.20, §6.2.22, §6.2.33 | Deferred. No non-3GPP access NF. |
| 2 | `access/epc` (MME, EMM, ESM, S1AP, N26) | EPC interworking with and without N26; ePDG | TS 23.501 §4.3, §4.3.3, §4.3.4, §5.17.2, §5.17.2.2, §5.17.2.3 | Deferred. AMF has no N26; no MME (an MME is EPC, out of 5GC scope, so only the 5GC side of N26 is in scope). |
| 3 | `wifi_offload` | ATSSS (multi-access PDU sessions) | TS 23.501 §4.2.10, §5.32 | Deferred. |
| 4 | `pws` | Public Warning System (AMF N50 to CBCF) | TS 23.501 §4.4.1, §5.16.1; TS 23.041 **unverified** (fetch failed) | Deferred. |
| 5 | `safety/mbs` | 5MBS | TS 23.501 §4.2.11; TS 23.247 **unverified** (fetch failed) | Held by ADR-0546. |
| 6 | `disaster_roaming` | Disaster roaming / MINT | TS 23.501 §5.40 | Deferred. |
| 7 | `iops` | Isolated E-UTRAN / NR operation for public safety | TS 23.180 **unverified** (fetch failed; TS 23.501 has no IOPS section) | Deferred, citation to be verified before work starts. |
| 8 | `npn` | SNPN and PNI-NPN (CAG) | TS 23.501 §5.30, §5.30.2, §5.30.3 | Partial: CAG info provisioning in the NEF, CAG Ack in the UDM. SNPN and AMF CAG enforcement deferred. |
| 9 | `ran_sharing` | Network sharing | TS 23.501 §5.18 | Deferred. |
| 10 | `access/ntn` | NR satellite access, satellite backhaul | TS 23.501 §5.4.10, §5.4.11, §5.4.13, §5.4.14, §5.43 | Deferred. |
| 11 | `redcap` | RedCap UEs | TS 23.501 §5.41 | Deferred. |
| 12 | `nbiot`, `nidd` | CIoT optimisations, NB-IoT, Cat-M | TS 23.501 §5.31, §5.31.10, §5.31.20 | Partial: NIDD (§5.31.5) in NEF/PCF/SMF/UDM/UDR. Other CIoT features deferred. |
| 13 | `racs` | Radio capability signalling optimisation, UCMF | TS 23.501 §5.4.4, §5.9.10, §6.2.21 | UCMF held by ADR-0546; AMF RACS handling deferred. |
| 14 | AMF MUSIM | Multi-USIM | TS 23.501 §5.38 | Deferred. |
| 15 | `nf/tsnaf`, `edge/tsn` (gPTP, BMCA) | TSN AF, TSC, 5G as a TSN bridge | TS 23.501 §4.4.8, §5.27, §5.28, §6.2.32 | Partial: TSCTSF exists (ADR-0546 completes it). TSN AF deferred. gPTP/BMCA run in the UPF/DS-TT data path and are deferred with the UPF fast path (ADR-0445). |
| 16 | (5G LAN) | 5G LAN-type service, 5G VN groups | TS 23.501 §4.4.6, §5.29, §5.8.2.13 | Partial: VN group data in NEF/UDM/UDR. SMF/UPF 5G LAN forwarding deferred. |
| 17 | `positioning/ranging` | Ranging / sidelink positioning | TS 23.501 §4.2.17 | Partial: CHF ranging-SL charging detection, NEF UeIdMappingInfo. Rest deferred. |
| 18 | `prose` | ProSe, 5G DDNMF, PKMF | TS 23.501 §4.2.12, §6.2.30 | Held by ADR-0546. |
| 19 | `emergency` | Emergency services, MPS | TS 23.501 §5.16.4, §5.16.5 | Deferred. Emergency appears only as LI request types in the SMF. |
| 20 | `mcx`, `frmcs` | Mission critical (MCPTT/MCVideo/MCData) over 5GS; FRMCS as its railway use | TS 23.501 §5.16.6; TS 23.289; TS 23.379 | Deferred. These are application-layer (MC system) specs; the core's part is QoS/priority and the §5.16.6 handling. FRMCS is not a separate 3GPP core function. |
| 21 | `services/ims` | IMS CSCF, P-CSCF discovery | TS 23.501 §4.4.3, §5.16.3, §5.16.3.11 | Held by ADR-0545. P-CSCF data exists in PCF/UDM. |
| 22 | `edge/eas`, `mec` | Edge: EAS discovery, EASDF | TS 23.501 §4.2.13, §5.13, §6.2.31 | Held by ADR-0544 and ADR-0546. |
| 23 | `pin` | Personal IoT Networks | TS 23.501 §5.44 | Held by ADR-0546. |
| 24 | (EIF) | Energy information function, energy efficiency | TS 23.501 §4.2.18, §5.51, §6.2.34; TS29566 YAML is local | Deferred (ADR-0546 lists TS29566). |
| 25 | (URLLC) | Redundant transmission for URLLC | TS 23.501 §5.33 | Deferred. Only the PCF carries redundancy fields today. |
| 26 | `uas`, `v2x`, `ambient`, `seal` | UAS, V2X, Ambient IoT, SEAL-type enablers | TS 23.501 §4.2.14 and the track ADRs | Held by ADR-0540..0544. |
| 27 | `nsaas` | Network slice as a service | GSMA / TM Forum, not a 3GPP core function | Deferred to the BSS layer (TMF APIs), not the core. NSAC (§5.15.11) already exists as the NSACF. |
| 28 | `ussd`, `supplementary` | USSD / USSI over IMS | TS 24.390 **unverified** (fetch failed) | Deferred; IMS-dependent (ADR-0545). |
| 29 | `esim` | eSIM remote provisioning (SM-DP+) | GSMA SGP.22, not 3GPP | Out of core scope; recorded for the BSS layer. |
| 30 | `dpi` | Deep packet inspection | Not a 3GPP function (application detection in the UPF is TS 23.501 PFD handling) | Out of scope as a product; PFD-based detection stays with the UPF. |
| 31 | `sensing/isac` | Integrated sensing and communication | Study only in R19; **no normative section** in TS 23.501 | Not claimed. Recorded so it is not mistaken for a missing R19 item. |
| 32 | IAB / NCR / MWAB / Femto | RAN-node variants | TS 23.501 §5.35, §5.47, §5.49, §5.50 | Deferred; the core's part is AMF/SMF support for these nodes. |
| 33 | UE / gNB tester | Built-in RAN and UE simulator | -- | Not adopted: the lab uses UERANSIM (open source). |

**Disclosed.**

- Four citations are unverified because the 3GPP FTP fetch failed: TS 23.041, TS 23.180, TS 23.247, TS 24.390. Each will be fetched and checked before its row starts; until then no section number is given for them.
- "Partial" rows name what exists in `nfs/*/src`; they were found by grep, not by a conformance test.
- The survey is one snapshot of the other repository's `main` on 2026-11-01.

**Rejected alternatives.**

- *Porting the Go modules.* Rejected: AGPL-3.0, and our APIs are generated from the 3GPP YAML, never hand-ported.
- *Adopting mmt-studio-5g6g wholesale or as a dependency.* Rejected for the same licence reason, and because its modules have no tests.
- *Ignoring it.* Rejected: it is a useful checklist of R19 topics, so each topic is mapped to the spec and recorded here.
- *Opening one ADR per row now.* Rejected: each row gets its own ADR and procedure list when its turn comes (guardrail 2).
