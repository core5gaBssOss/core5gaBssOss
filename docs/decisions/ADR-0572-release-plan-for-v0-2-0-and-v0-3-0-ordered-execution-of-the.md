## ADR-0572: Release plan for v0.2.0 and v0.3.0: ordered execution of the 2026-11-10 in-depth evaluation

**Date:** 2026-11-10. **Status:** Proposed (user, 2026-11-10: "recommend the order of execution
... I need to finish and release the next version fast. Please record as ADR"). Follows ADR-0009
(production-grade target), ADR-0467/ADR-0509 (how releases are cut, `docs/RELEASING.md`), v0.1.0
(2026-10-31) and REVIEW-5ae0ef1.

**Context.** On 2026-11-10 the project was evaluated in depth against its own target (ADR-0009):
`docs/COMPONENT_STATUS.md`, the REVIEW-5ae0ef1 ledger, the README status rows,
`docs/COMPLIANCE_P1_P15.md`, `docs/R19_COMPLETENESS.md` and `docs/TRACEABILITY.md` were read and
then checked against the code, tests, CI and deployment files at `main` 478ea5e5. Most of what
follows the repository already discloses; this ADR records it in one place and fixes the order in
which it is worked off. Wording per project rule: open items are *deferred for future
implementation*.

*A. Items that stand between the project and a real deployment.*

| # | Item (deferred for future implementation) | Evidence | Disclosed before |
|---|---|---|---|
| A1 | User plane is uplink-only: GTP-U decapsulation into a TUN device; no downlink (N6 to N3 encapsulation), no gNB-facing N3, IPv4 only, 4 of ~46 TS 28.552 GTP measurements | `nfs/upf/src/datapath.hpp`; README Phase 3; `docs/PERFORMANCE_MAPPING.md` | yes |
| A2 | No paging / network-triggered service request (TS 23.502 4.2.3.3): MT reachability is `UNREACHABLE` without a live NGAP association | TRACEABILITY "no paging"; one `paging` mention in `nfs/amf/src` | in part |
| A3 | PDU session type IPv4 only: no IPv6, IPv4v6, Ethernet or Unstructured (TS 23.501 5.6.10, 5.8.2.2), SSC mode 1 only, no ULCL/multi-homing; 5 of 29 NFs register `ipv6Addresses` | `nfs/smf/src/main.cpp` | no |
| A4 | Single replica by design, Kubernetes never run: every chart `replicas: 1`, AMF/NRF state in memory; 36 charts `helm lint`/`template` only; no chart for Postgres, Valkey, Doris, Kafka or ClickHouse (Keycloak's Postgres excepted); geo-redundancy (P11) deferred by the user 2026-09-05 | `deploy/helm/*/templates/deployment.yaml`; ADR-0513/0515/0516; `docs/COMPLIANCE_P1_P15.md` | yes |
| A5 | Lawful interception "still a deployment blocker": CC-POI in the UPF is never called from the datapath; SIRF in the NRF not built | README LI row; COMPONENT_STATUS rows 51-52 | yes |
| A6 | No EPC interworking (N26), non-3GPP access, emergency services, PWS, ATSSS, SNPN, satellite | COMPONENT_STATUS rows 87-104, ADR-0550 | yes |

*B. Items against the production-grade bar.*

| # | Item (deferred for future implementation) | Evidence |
|---|---|---|
| B7 | SBI error conformance half done: 546 of 986 error responses carry no TS 29.500 table 5.2.7.2-1 `cause` (SBI-03). NRF PATCH non-conformance and NRF DELETE 204 open. The ledger row SPEC-05 is stale: ADR-0521/0522/0523 closed it | `docs/reviews/REVIEW-5ae0ef1.md` |
| B8 | Secrets are file-mounted passwords; OpenBao (ADR-0560) pipeline only; per-container secret separation in compose open (SEC-04) | ledger SEC-04; ADR-0560 |
| B9 | Test evidence: no coverage measurement; libFuzzer targets never run in CI; end-to-end NAS/NGAP driven by the project's own `tests/integration/ue_nas_driver.cpp`, UERANSIM by hand only and `simulators/ransim/README.md` says its configs are not in the repo | `tests/fuzz/CMakeLists.txt`; `.github/workflows/ci.yml` |
| B10 | CI is one self-hosted runner on the developer's machine, ~2.5 h per run, serial | `/home/mastermind/actions-runner` |
| B11 | Operations: no backup/restore, DR, rolling-upgrade or schema-migration procedure; schemas are `CREATE TABLE IF NOT EXISTS` files; config changes need a restart | `deploy/db/*`; ADR-0569 |
| B12 | Performance evidence covers the NRF only; "700M CDR" is a design claim (DOC-07); UPF/AMF/SMF never measured; TS 28.552 SMF 16 of ~54 | `docs/BENCHMARK_RESULTS.md`; `docs/PERFORMANCE_MAPPING.md` |

*C. Breadth, honestly recorded as pipeline.* 72 of 112 COMPONENT_STATUS rows are "Already in
Pipeline", 5 partial, 35 built; six Tier 2 NFs unbuilt (EASDF, UCMF, NSSAAF, MB-SMF/MB-UPF,
MBSF/MBSTF); 305 REL-19 API files registered, none built. BSS: TMF620/622/630/632/651/654/678
built, TMF640 in progress (ADR-0569); TMF633/635/637/638/639/666/676/688/727 of CLAUDE.md absent;
`docs/CHARGING_MAPPING.md` still has TODO rows. Operator GUI: four-eyes editing live for
`product-catalog` only.

*D. Ledger drift.* Three records lag the code: ledger row SPEC-05 (above), the ransim README,
`scripts/pipeline-clean.sh` (NOAUTH since ADR-0514, found 2026-11-09, ADR-0571).

*What holds up.* Spec fidelity (no invented field found in two reviews), mTLS and ES256 OAuth2
with audience/scope/subject checks, the CHF (converged charging, Gy, CAP, TS 32.298 CDRs, TAP3,
bill run), N2 and Xn handover, SEPP N32 with PRINS, LI ADMF/MDF2/MDF3, the codegen spine, and the
habit of disclosing stubs in ADRs.

**Decision:** three releases, each with a fixed scope, cut per `docs/RELEASING.md`. v0.2.0 closes
what is already in flight and the small follow-ups, so it can ship in days, not months; the user
plane goes to v0.3.0; operability to v0.4.0. Nothing from A or B is silently dropped: every item
has a release or is marked pipeline.

*v0.2.0: control plane, charging and provisioning close-out.* Order of execution, one PR each,
sizes S (one session), M (two or three sessions):

1. S. Ledger hygiene: ledger row SPEC-05 marked closed by ADR-0521/0522/0523; `simulators/ransim/README.md`
   current, `config/gnb.yaml`/`ue.yaml` committed; `scripts/pipeline-clean.sh` authenticates as the
   CHF ACL user; `CHANGELOG.md` *Unreleased* carries ADR-0566..0571 (#13-#18); COMPONENT_STATUS
   date. Docs and scripts only.
2. S. NRF `PATCH /nf-instances/{id}` conformance and `DELETE` 204; the #15 class (decoded path
   values forwarded into another NF's URI, ADR-0570) audited in every NF that builds a URI from a
   request path, with a test that fails without the fix.
3. M. TMF640 `PATCH /service/{id}` (mapping row A4, `odbPacketServices`), then TMF640 DELETE and
   notifications, in the UDR-split shape of ADR-0569. This completes the provisioning story that
   v0.2.0 is named for. TMF664 (ADR-0558 decision 5) stays after it and may slip to v0.3.0.
4. M. SBI-03: the remaining 546 error responses get their table 5.2.7.2-1 `cause`, one NF per PR,
   generator-assisted where the handler is generated.
5. S. Test evidence that costs no runner time per PR: libFuzzer targets as a nightly workflow on
   the same runner (fixed time budget per target), and a recorded UERANSIM registration plus PDU
   session run in `simulators/ransim/README.md` with the exact commands.
6. S. Release PR: version 0.2.0 in `CMakeLists.txt` and `vcpkg.json` (last PR, because the
   `vcpkg.json` change invalidates the CI cache and that run rebuilds the dependencies),
   `CHANGELOG.md` section, tag, release notes per `docs/RELEASING.md`. The notes repeat the v0.1.0
   sentence: aims at production grade, not deployable in production yet.

*v0.3.0: two-way user plane and idle mode.* Order fixed by dependency; each item gets its own
procedure list for approval before code (CLAUDE.md guardrail 2):

1. UPF downlink: N6 to N3 GTP-U encapsulation from the FAR, a gNB-facing N3 toward the UERANSIM
   gNB, PDR/FAR/QER for the downlink direction, ping through the UE TUN as the end-to-end test
   (A1). TS 29.244 for PFCP, TS 29.281 for GTP-U.
2. SMF PDU session types IPv6 and IPv4v6 with IPv6 address management (TS 23.501 5.8.2.2) and
   `ipv6Addresses` in every NF profile (A3). Ethernet and Unstructured stay pipeline.
3. Paging and network-triggered service request (TS 23.502 4.2.3.3; NGAP Paging, TS 38.413) (A2).
4. LI CC-POI called from the UPF datapath for the intercepted UE's PDRs (A5, needs 1).
5. UPF and SMF TS 28.552 measurements from the datapath, and a first UPF throughput number
   measured, not claimed (B12).

*v0.4.0: operability.* AMF/NRF state externalised, a kind cluster run with datastore charts and at
least two replicas of one stateless NF first (A4); OpenBao (ADR-0560) and compose secret
separation (B8); a schema-migration tool, backup/restore and upgrade procedure (B11); coverage
measurement in the nightly workflow (B9). CI moves off the developer's machine when a second
runner exists (B10).

*Pipeline, no release assigned (ADR-0550 scope).* A6 in full, the six Tier 2 NFs, the 305 REL-19
API files, the absent TMF APIs, the engineering console. These stay "Already in Pipeline" in
COMPONENT_STATUS.

**Rejected alternatives.**

- **Holding v0.2.0 until the user plane carries traffic.** It would delay a release by the whole
  of v0.3.0 while eighteen merged PRs since v0.1.0 (TMF codegen, TMF640, UDM path encoding,
  Valkey ACL, NWDAF/CHF fixes) go unrecorded. The user asked for a fast release.
- **One large release that closes A and B together, called 1.0.** It contradicts the small,
  reviewable increments of CLAUDE.md, and a 1.0 would claim production grade without the evidence
  (guardrail 6).
- **Kubernetes and high availability before the user plane.** Without two-way traffic there is
  nothing for a second replica to carry, and the host has no kind, kubectl or helm.
- **Fuzzing on every PR.** One runner, ~2.5 h per run; nightly on a time budget costs nothing per
  PR.
- **Dropping A6 or the 305 API files from the plan as out of scope.** They stay pipeline per
  ADR-0550; the project's scope is unchanged, only its order.

**Disclosed.**

- The sizes S and M are estimates from this session's PR history (#13-#18), not measurements. v0.3.0
  item 1 is the one piece of new kernel-side work; it is not sized here.
- No test or code changes in this ADR's PR; it fixes the plan only. The v0.1.0 release notes'
  sentence "not deployable in production yet" stays true through v0.2.0 and v0.3.0.
- TMF664 is placed in v0.2.0 item 3 with permission to slip; everything else in v0.2.0 is fixed.

**Progress (2026-11-20, `main` cc995945; updated 2026-11-23, `main` 3837d995, and 2026-11-27, `main` 9022f3a6 then 4182d213).**

| v0.2.0 item | State |
|---|---|
| 1 Ledger hygiene | Merged, #20. |
| 2 NRF PATCH/DELETE; ADR-0570 audit | NRF merged, #21 (ADR-0574). Audit: NEF part merged, #22 (ADR-0575). The body-, record- and peer-derived URL sites listed in ADR-0575 *Disclosed* (NEF, AUSF, SMF, AMF, bss/provisioning, PCF) are deferred for future implementation. AUSF part merged, #54 (ADR-0603, d55cc7b0). SMF #57 (ADR-0604), AMF #56 (ADR-0605) and NEF part 2 #55 (ADR-0606) open, each test run in the main checkout with a revert check. bss/provisioning not yet audited; the PCF encodes (ADR-0549). Update 2026-11-23: NEF part 2 merged, #55 (ADR-0606, 9d50eead); SMF part merged, #57 (ADR-0604, 5d61375c); AMF #56 (ADR-0605) still open. bss/provisioning audited with no change needed: the SUPI is validated as `^imsi-[0-9]{5,15}$` before any URL is built (`bss/provisioning/src/tmf640_service.cpp:69`, `bss/provisioning/src/provisioning_store.cpp:111`), so it cannot carry `/`, `?`, `#` or `%`. Update 2026-11-27: AMF part merged, #56 (ADR-0605, 9119d75f). The PCF is the only site still deferred. |
| 3 TMF640 PATCH, DELETE, notifications | PATCH merged, #23 (ADR-0576); DELETE merged, #34 (ADR-0585); notifications merged, #35 (ADR-0586). TMF664 may slip to v0.3.0, as planned. |
| 4 SBI-03 causes | Merged: NRF #24 (ADR-0577), NEF AM policy #25 (ADR-0578), UDR #32, UDM #33, PCF #36, AMF #37, SMF #38, NWDAF/BSF #39, SMSF #41, NSSF #42, AUSF/MFAF/CHF #43, ADRF #44, DCCF #45, EIR #47, GMLC #49 (ADR-0583, 0584, 0587..0595, 0597, 0599). LMF #50 (ADR-0600) merged. NSACF needs no change. NEF part 2 merged, #52 (ADR-0601): 50 more NEF 404s carry their cause, the other 121 have none defined. UDM 502 for a UDR 503/429: #53 (ADR-0602) merged ea82f83f. Open: `invalidParams`; BSS services are an open question. Update 2026-11-23: `invalidParams` (TS 29.500 Table 5.2.7.2-1 NOTE 1) step 1 merged, #58 (ADR-0607, ba6519ad: sbi-core overload and builders); step 2 merged for the UDR, #60 (ADR-0609, b076fd2c, with the Subs To Notify route fix) and the SEPP, #62 (ADR-0610, 3837d995); step 2 open for UDSF #63 (ADR-0608), NRF #64 (ADR-0613), UDM #67 (ADR-0614) and SMF #72 (ADR-0621). The BSS question is answered (user, 2026-11-21, recorded in ADR-0612): TMF servers answer the TM Forum `Error`. BSS series: PR 0 merged, #59 (ADR-0611, 8469c313: TMF620/632/651/654 swaggers vendored); PR 1 merged, #61 (ADR-0612, fa0e5ac6: `libs/tmf-core`); PRs 2-6 open (#66 ADR-0615 provisioning, #65 ADR-0616 roaming-interconnect, #71 ADR-0618 balance-management, #69 ADR-0619 product-catalog, #70 ADR-0620 subscriber-management). The `parse_json_body` 422s without invalidParams stay deferred for future implementation (ADR-0607). Update 2026-11-27: step 2 merged for the UDSF #63 (ADR-0608, 899f4250), NRF #64 (ADR-0613, 4cb7a62e) and UDM #67 (ADR-0614, 20a898d0); BSS PR 3 merged, roaming-interconnect #65 (ADR-0616, 9022f3a6). Step 2 open: SMF #72 (ADR-0621), NEF #74 (ADR-0623), ADRF #75 (ADR-0624), NWDAF #76 (ADR-0625), PCF #78 (ADR-0626), AUSF #77 (ADR-0627), BSF #80 (ADR-0628), NSSF #79 (ADR-0629), CHF #81 (ADR-0630), SMSF #83 (ADR-0632), EIR #82 (ADR-0633), GMLC #84 (ADR-0634), AMF #85 (ADR-0635), LMF #86 (ADR-0636), MFAF #87 (ADR-0637), TSCTSF #88 (ADR-0638), AAnF #89 (ADR-0639). With these every NF that answers a NOTE 1 cause has step 2 merged or open; the DCCF, SCP and UPF need none (disclosed in ADR-0634, open PR #84). BSS PRs 2, 4, 5, 6 open (#66, #71, #69, #70); PR 7 waits for them. Later 2026-11-27 (`main` 4182d213): step 2 merged for the NEF #74 (ADR-0623), GMLC #84 (ADR-0634), AMF #85 (ADR-0635), LMF #86 (ADR-0636), MFAF #87 (ADR-0637), TSCTSF #88 (ADR-0638), AAnF #89 (ADR-0639); BSS PRs 2 and 5 merged (#66 ADR-0615, #69 ADR-0619). Step 2 still open: SMF #72 (ADR-0621), ADRF #75 (ADR-0624), NWDAF #76 (ADR-0625), PCF #78 (ADR-0626), AUSF #77 (ADR-0627), BSF #80 (ADR-0628), NSSF #79 (ADR-0629), CHF #81 (ADR-0630), SMSF #83 (ADR-0632), EIR #82 (ADR-0633). BSS PRs 4 and 6 open (#71, #70). |
| 5 Fuzz nightly; recorded UERANSIM run | Both merged: #26 (ADR-0579) and #27. |
| 6 Release PR | Open; it comes last. |

B10 has changed. There are now two self-hosted runners, the lab host and the laptop `mastermind-073-small`
(ADR-0580, ADR-0581, ADR-0582). Both still sit on the developer's own machines, so B10 itself stays open.
Update 2026-11-23 (user decisions 2026-11-22/23): `5gc-lab-host` (007, labels `5gc-lab` + `5gc-lab-small`) runs build, sanitize and lint; `mastermind-073-small` (073) carries only `5gc-lab-small` since 2026-11-23 and runs lint only, because ASan jobs there (6.9 GB RAM, Doris beside the tests) kept failing while spawned NFs took ~30 s to start (PRs #56, #58, #61, #60); the master's runner `5gc-lab-mastermindAI` carries only `5gc-master` and is fallback only (CI labels added 2026-11-21, removed 2026-11-22 after port contention with local tests). ADR-0617 (#68, db66244e) made the integration tests' readiness waits deadline-based, 3x under ASan and 5x under TSan. Build and sanitize jobs are therefore serial on one machine again; B10 stays open.
Update 2026-11-27: `mastermind-073-small` carries `5gc-lab` + `5gc-lab-small` again (`5gc-lab` re-added at the user's request; seen on the runner list 2026-11-27), so build and sanitize jobs run on 007 and 073 side by side again; the master is unchanged (`5gc-master` only, fallback). B10 stays open: both runners are still the developer's own machines.
B9 has changed in part: the libFuzzer targets now have a nightly workflow, but coverage is still not measured.
