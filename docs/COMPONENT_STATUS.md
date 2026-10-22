# Component status

The complete component list for this project, in scope per `CLAUDE.md` and
`docs/project-context/scope-tier2-tier3.md`, with the build status of each.

- ✅ **Built**: the component's code exists in this repo. It does not mean feature-complete; each
  component's ADRs and `docs/TRACEABILITY.md` record what it covers and what it does not.
- ⚠️ **Partial**: some of the component exists; the gap is stated.
- 🕒 **Already in Pipeline**: in scope, no code in the repo yet.

**Keep this file current.** When a component is added or its status changes, update its row and the
totals in the same commit.

Last updated: 2026-10-14.

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
| 29 | SEPP (vSEPP / hSEPP, N32) | ⚠️ Partial: N32-c Security Capability Negotiation and N32-f forwarding with TLS security (ADR-0491); telescopic FQDN, only the `Location` header rewritten, not URIs in bodies (ADR-0492); PRINS parameter exchange and JOSE-protected forwarding of JSON bodies (ADR-0493/0494). Not built: roaming intermediaries, N32-f keepalive PINGs, TLSCOR OPTIONS, SNDN32F, topology hiding, message filtering, the token PLMN-ID check on the TLS path (PRINS path only), URI_PARAM and multipart protection, replay detection, renegotiation, SeppInfo in the NRF profile (ADR-0491, ADR-0494 Disclosed). Open review findings: SEC-01 partly fixed, SEC-11, SEC-12, SEC-14 (see below) |

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

## Totals

| Group | Built | Already in pipeline |
|---|---|---|
| Network functions, Tiers 1–3 | 23 of 48 (+ SEPP, AAnF, TSCTSF partial) | 22 |
| LI functions | 3 complete, 2 partial | 1 (SIRF) |
| Charging/BSS and platform | 9 of 9 | 0 |

The SEPP carries inter-PLMN SBI traffic over N32 with TLS security or PRINS (application-layer
security) between two SEPPs in `SeppEndToEnd`; the compose lab has no partner SEPP. Roaming
intermediaries are one N32 gap of several: row 29 lists what is not built. Of the SEPP findings in
`docs/reviews/REVIEW-5ae0ef1.md`: SEC-02 (N32-f correlation across peers) is fixed; SEC-01 is partly
fixed -- the N32-c `sender` must be a client-certificate dNSName SAN, but per-PLMN trust anchors are
not built and the lab's one shared `sepp` certificate names both lab SEPPs, so in the lab either can
still claim the other; SEC-11 (case-sensitive `Authorization` checks), SEC-12 (unbounded telescopic
table) and SEC-14 (handshake ids from a non-cryptographic RNG) are open, and SUS-01 (a long `Via`
header and `std::regex`) is unconfirmed.
