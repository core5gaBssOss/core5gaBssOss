# Component status

The complete component list for this project, in scope per `CLAUDE.md` and
`docs/project-context/scope-tier2-tier3.md`, with the build status of each.

- ✅ **Built**: the component's code exists in this repo. It does not mean feature-complete; each
  component's ADRs and `docs/TRACEABILITY.md` record what it covers and what it does not.
- ⚠️ **Partial**: some of the component exists; what is deferred for future implementation is stated.
- 🕒 **Already in Pipeline**: in scope, no code in the repo yet.

**Keep this file current.** When a component is added or its status changes, update its row and the
totals in the same commit.

Last updated: 2026-11-07.

## 5G Core, Tier 1: all 13 built

| # | Component | Status |
|---|---|---|
| 1 | NRF | ✅ Built |
| 2 | AMF | ✅ Built |
| 3 | SMF | ✅ Built |
| 4 | UPF | ✅ Built |
| 5 | PCF | ✅ Built |
| 6 | UDM | ✅ Built |
| 7 | UDR | ✅ Built |
| 8 | AUSF | ✅ Built |
| 9 | NSSF | ✅ Built |
| 10 | NEF | ✅ Built |
| 11 | SCP | ✅ Built |
| 12 | BSF | ✅ Built |
| 13 | CHF | ✅ Built |

## 5G Core, Tier 2: 10 built, 3 partial, 3 already in pipeline

| # | Component | Status |
|---|---|---|
| 14 | NWDAF (AnLF + MTLF) | ✅ Built |
| 15 | DCCF | ✅ Built |
| 16 | ADRF | ✅ Built |
| 17 | MFAF | ✅ Built |
| 18 | NSACF | ✅ Built |
| 19 | TSCTSF | ⚠️ Partial: Ntsctsf_QoSandTSCAssistance (AF requested QoS by UE address or GPSI) with PCF-BSF binding registration and NEF GPSI routing (ADR-0498); TSC assistance, time synchronization, ASTI in pipeline |
| 20 | EASDF | 🕒 Already in Pipeline |
| 21 | UCMF | 🕒 Already in Pipeline |
| 22 | SMSF | ✅ Built |
| 23 | 5G-EIR | ✅ Built |
| 24 | LMF | ✅ Built |
| 25 | GMLC | ✅ Built |
| 26 | NSSAAF | 🕒 Already in Pipeline |
| 27 | AAnF | ⚠️ Partial: AKMA anchor key, application key, context removal with the AUSF/UDM sides, GPSI, NEF path, roaming check (ADR-0495..0497); service-disable notification in pipeline |
| 28 | UDSF | ✅ Built |
| 29 | SEPP (vSEPP / hSEPP, N32) | ⚠️ Partial: N32-c + N32-f with TLS security (ADR-0491), telescopic FQDN (ADR-0492), PRINS with JOSE-protected forwarding (ADR-0493/0494), certificate-bound N32-c sender and N32-f correlation (ADR-0491), per-PLMN trust anchors (ADR-0529); not built: roaming intermediaries, N32-f keepalive PINGs, TLSCOR OPTIONS, SNDN32F, topology hiding, message filtering, URI_PARAM and multipart protection, replay detection, renegotiation, the N32-f/N32-c certificate PLMN subset check |

## 5G Core, Tier 3: all 18 already in pipeline

| # | Component | Status |
|---|---|---|
| 30 | MB-SMF (5MBS) | 🕒 Already in Pipeline |
| 31 | MB-UPF (5MBS) | 🕒 Already in Pipeline |
| 32 | MBSF (5MBS) | 🕒 Already in Pipeline |
| 33 | MBSTF (5MBS) | 🕒 Already in Pipeline |
| 34 | AIOTF (Ambient IoT, TS 23.369) | 🕒 Already in Pipeline |
| 35 | PKMF (ProSe) | 🕒 Already in Pipeline |
| 36 | PAnF (ProSe) | 🕒 Already in Pipeline |
| 37 | SLPKMF (sidelink positioning) | 🕒 Already in Pipeline |
| 38 | IMS AS (Data Channel) | 🕒 Already in Pipeline |
| 39 | MF (Data Channel) | 🕒 Already in Pipeline |
| 40 | MSGin5G | 🕒 Already in Pipeline |
| 41 | MNPF | 🕒 Already in Pipeline |
| 42 | PIN server | 🕒 Already in Pipeline |
| 43 | SEAL | 🕒 Already in Pipeline |
| 44 | SEALDD | 🕒 Already in Pipeline |
| 45 | ADAES | 🕒 Already in Pipeline |
| 46 | CAPIF | 🕒 Already in Pipeline |
| 47 | EIF (N110–N114) | 🕒 Already in Pipeline |

## Lawful Interception (TS 33.126 / 33.127 / 33.128)

| # | Component | Status |
|---|---|---|
| 48 | LI-ADMF (with LIPF) | ✅ Built |
| 49 | LI-MDF2 | ✅ Built |
| 50 | LI-MDF3 | ✅ Built |
| 51 | SIRF (in NRF) | 🕒 Already in Pipeline. TS 33.128 leaves its LI_SI interface "for further study". |
| 52 | IRI-POIs in NFs | ⚠️ Partial: built in AMF, SMF, UDM, SMSF, NEF, CHF and NWDAF |
| 52 | CC-POI in UPF | ⚠️ Partial: the POI exists, but nothing in the UPF datapath calls it |

## Charging and BSS (3GPP + TM Forum SID): all 6 built

| # | Component | Status |
|---|---|---|
| 53 | Online Charging (Gy Diameter CCR/CCA, CAP) in the CHF | ✅ Built |
| 54 | Product Catalog | ✅ Built |
| 55 | Balance Management | ✅ Built |
| 56 | Subscriber Management | ✅ Built |
| 57 | Provisioning | ✅ Built |
| 58 | Roaming Interconnect | ✅ Built |

## Platform and tooling: all 3 built

| # | Component | Status |
|---|---|---|
| 59 | Operator GUI (web + BFF) | ✅ Built |
| 60 | RAN simulator | ✅ Built |
| 61 | hello-nf (test/demo) | ✅ Built |

## R19 completeness (ADR-0539): all 25 in pipeline

Added 2026-11-01. Every REL-19 OpenAPI file that no code uses yet is listed, with its owning ADR, in
[`R19_COMPLETENESS.md`](R19_COMPLETENESS.md). The rows below are the components behind those files
that rows 1–61 do not already cover (EASDF, UCMF, NSSAAF, 5MBS, AIOTF, ProSe keys, IMS data channel,
MSGin5G, MNPF, PIN, SEAL, SEALDD, ADAES, CAPIF and EIF are rows 20–47).

| # | Component | Status |
|---|---|---|
| 62 | V2X / A2X in the PCF, AMF, NRF and CHF (TS 29.525, 29.518, 29.510, 32.291) | 🕒 Already in Pipeline (first; the UDM/UDR V2X/A2X subscription data is in), ADR-0540; in progress: PCF increment 1 of 3 (V2X/A2X eligibility, N2 PC5 policy via N1N2MessageTransfer, PLMN change re-provisioning; the A2X N2 policy is deferred for future implementation), ADR-0551; increment 2 of 3 (UDR serviceParamData read and change subscription, V2XP/A2XP in MANAGE UE POLICY COMMAND over N1 UPDP, COMPLETE/REJECT via N1MessageNotify), ADR-0555; increment 3 of 3 in progress (UE POLICY PROVISIONING REQUEST answered with COMMAND or REJECT; after a delivery failure the PCF provisions CON_STATE_CH by UpdateNotify and re-delivers when the UE is CONNECTED; UDR-triggered re-delivery and the N2 PC5 re-send are still to come), ADR-0563 |
| 63 | USS `Naf_Authentication` (UAS, TS 29.255) | 🕒 Already in Pipeline, ADR-0541 |
| 64 | UAE server (UAS, TS 29.257) | 🕒 Already in Pipeline, ADR-0541 |
| 65 | UAV fields in the AMF and UDM (UAS) | 🕒 Already in Pipeline (the NEF's UUAA and flight assistance are in), ADR-0541 |
| 66 | VAE server (V2X application enabler, TS 29.486) | 🕒 Already in Pipeline, ADR-0542 |
| 67 | `Nadm_Sec` (Ambient IoT, TS 29.369) | 🕒 Already in Pipeline, ADR-0543 |
| 68 | Ambient IoT fields in the NRF, NSSF and CHF | 🕒 Already in Pipeline (the AMF, NEF and UDR AIoT parts are in), ADR-0543 |
| 69 | EES / ECS (edge enablement, TS 29.558, TS 24.558) | 🕒 Already in Pipeline, ADR-0544 |
| 70 | NSCE server (TS 29.435) | 🕒 Already in Pipeline, ADR-0544 |
| 71 | SM and SAn servers (spatial map and anchors, TS 24.550, TS 29.437) | 🕒 Already in Pipeline, ADR-0544 |
| 72 | AIMLE server (TS 29.482, TS 24.560) | 🕒 Already in Pipeline, ADR-0544 |
| 73 | LMS (TS 24.283) | 🕒 Already in Pipeline, ADR-0544 |
| 74 | ETC configuration (TS 24.549) | 🕒 Already in Pipeline, ADR-0544 |
| 75 | SS AADRF (TS 29.549) | 🕒 Already in Pipeline, ADR-0544 |
| 76 | VFL inference `Naf_VFLInference` (TS 29.530) | 🕒 Already in Pipeline, ADR-0544 |
| 77 | HSS `Nhss` services (TS 29.562, TS 29.563) | 🕒 Already in Pipeline, ADR-0545 |
| 78 | GBA BSF (TS 29.309) | 🕒 Already in Pipeline, ADR-0545 |
| 79 | SMS router (TS 29.577) | 🕒 Already in Pipeline, ADR-0545 |
| 80 | IWK-MSC `Niwmsc` (TS 29.579) | 🕒 Already in Pipeline, ADR-0545 |
| 81 | MMTel call control (TS 29.392) | 🕒 Already in Pipeline, ADR-0545 |
| 82 | 5G DDNMF (ProSe, TS 29.555) | 🕒 Already in Pipeline, ADR-0546 |
| 83 | ProSe AF `Naf_ProSe` (TS 29.557) | 🕒 Already in Pipeline, ADR-0546 |
| 84 | SoR AF (TS 29.550) | 🕒 Already in Pipeline, ADR-0546 |
| 85 | OAM management services (TS 28.xxx REL-19 APIs) | 🕒 Already in Pipeline, ADR-0547 |
| 86 | 5G media streaming and data collection (TS 26.xxx REL-19 APIs) | 🕒 Already in Pipeline, ADR-0547 |

Added 2026-11-01 from the mmt-studio-5g6g functionality survey (ADR-0550). Each row is a 3GPP
feature the core does not implement yet; partial ones name what is already in on ADR-0550's table.

| # | Component | Status |
|---|---|---|
| 87 | Non-3GPP access: N3IWF, TNGF, TWIF, W-AGF (TS 23.501 §4.2.8) | 🕒 Already in Pipeline, ADR-0550 |
| 88 | EPC interworking with and without N26 (TS 23.501 §4.3) | 🕒 Already in Pipeline, ADR-0550 |
| 89 | ATSSS multi-access PDU sessions (TS 23.501 §5.32) | 🕒 Already in Pipeline, ADR-0550 |
| 90 | Public Warning System, AMF to CBCF (TS 23.501 §5.16.1) | 🕒 Already in Pipeline, ADR-0550 |
| 91 | Disaster roaming / MINT (TS 23.501 §5.40) | 🕒 Already in Pipeline, ADR-0550 |
| 92 | SNPN and AMF CAG enforcement (TS 23.501 §5.30) | 🕒 Already in Pipeline, ADR-0550 |
| 93 | Network sharing (TS 23.501 §5.18) | 🕒 Already in Pipeline, ADR-0550 |
| 94 | NR satellite access and satellite backhaul (TS 23.501 §5.4.10, §5.43) | 🕒 Already in Pipeline, ADR-0550 |
| 95 | RedCap UEs (TS 23.501 §5.41) | 🕒 Already in Pipeline, ADR-0550 |
| 96 | CIoT optimisations beyond NIDD (TS 23.501 §5.31) | 🕒 Already in Pipeline, ADR-0550 |
| 97 | AMF RACS handling (TS 23.501 §5.4.4) | 🕒 Already in Pipeline, ADR-0550 |
| 98 | Multi-USIM (TS 23.501 §5.38) | 🕒 Already in Pipeline, ADR-0550 |
| 99 | TSN AF, 5G as a TSN bridge (TS 23.501 §5.27, §5.28) | 🕒 Already in Pipeline, ADR-0550 |
| 100 | 5G LAN-type service forwarding in SMF/UPF (TS 23.501 §5.29) | 🕒 Already in Pipeline, ADR-0550 |
| 101 | Emergency services and MPS (TS 23.501 §5.16.4, §5.16.5) | 🕒 Already in Pipeline, ADR-0550 |
| 102 | Mission critical services over 5GS (TS 23.501 §5.16.6; TS 23.289) | 🕒 Already in Pipeline, ADR-0550 |
| 103 | URLLC redundant transmission (TS 23.501 §5.33) | 🕒 Already in Pipeline, ADR-0550 |
| 104 | AMF/SMF support for IAB, NCR, MWAB and femto nodes (TS 23.501 §5.35, §5.47, §5.49, §5.50) | 🕒 Already in Pipeline, ADR-0550 |

## Reference-architecture adoptions

Added 2026-11-07 when ADR-0556..0562 were accepted. Accepting a decision does not build anything:
no code for these rows is in the repo yet, and each ADR records what stays deferred for future
implementation.

| # | Component | Status |
|---|---|---|
| 105 | Kafka event contracts in a schema registry (Apicurio) | 🕒 Already in Pipeline, ADR-0556 |
| 106 | Open Policy Agent, northbound and MCP tool authorisation | 🕒 Already in Pipeline, ADR-0557 |
| 107 | TMF640 Service Activation and TMF664 Resource Function Activation | 🕒 Already in Pipeline, ADR-0558. Mapping approved (docs/PROVISIONING_MAPPING.md); TMF640 DTOs generated (ADR-0568); TMF640 `POST /service`, `GET /service`, `GET /service/{id}` built (ADR-0569). PATCH, events and TMF664 not built yet |
| 108 | Debezium change data capture, PostgreSQL to Kafka, for BSS and analytics consumers | 🕒 Already in Pipeline, ADR-0559 |
| 109 | OpenBao for secrets | 🕒 Already in Pipeline, ADR-0560 |
| 110 | Feast feature store for the NWDAF and CHF training sidecars | 🕒 Already in Pipeline, ADR-0561 |
| 111 | MCP server over Streamable HTTP with Keycloak-issued tokens | 🕒 Already in Pipeline, ADR-0562 |

## Totals

| Group | Built | Already in pipeline |
|---|---|---|
| Network functions, Tiers 1–3 | 23 of 47 (+ SEPP, AAnF, TSCTSF partial) | 21 |
| LI functions | 3 complete, 2 partial | 1 (SIRF) |
| Charging/BSS and platform | 9 of 9 | 0 |
| R19 completeness (ADR-0539) | 0 of 25 | 25 |
| mmt-studio-5g6g survey (ADR-0550) | 0 of 18 | 18 |
| Reference-architecture adoptions (ADR-0556..0562) | 0 of 7 | 7 |

The SEPP now carries inter-PLMN SBI traffic over N32 with TLS security or PRINS (application-layer
security). It is partial: the unbuilt N32 items are listed in its row above (REVIEW-5ae0ef1 DOC-01).
