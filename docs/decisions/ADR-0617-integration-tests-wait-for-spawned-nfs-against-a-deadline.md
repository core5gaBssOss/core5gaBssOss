## ADR-0617: Integration tests wait for spawned NFs against a deadline that scales under sanitizers

**Date:** 2026-11-22. **Status:** Proposed.

**Context.** NF integration tests spawn real NF binaries and then poll until the NFs they need
answer. Each test file has its own copy of that poll, bounded by an attempt count. Under the
`sanitize (asan-ubsan)` job those polls gave up before the NF was up, and the job failed on tests
whose NF behaviour was never reached:

| CI run | Failing tests | Wait that gave up |
|---|---|---|
| PR #56, attempt 1 | `AmfNgapTestGnb.RegistrationAndPduSessionReachNsacf` (Subprocess aborted), `LiSmfEndToEnd.RealSmfReportsEstablishmentAndModificationOfATargetsPduSession` | `test_amf_ngap_handover.cpp:120` and `test_li_amf_e2e.cpp:229`, both on the UDR probe `:7781` |
| PR #56, attempt 2 | `AmfNgapTestGnb.FullN2HandoverRelayThroughExecutionAndSourceRelease` | `test_amf_ngap_handover.cpp:120`, UDR |
| PR #56, attempt 3 | `AmfNgapTestGnb.PduSessionIsRejectedWhenNsacfRefusesTheSlice`, `PduSessionRelease.UeRequestedReleaseCompletesThroughAmfAndSmf`, `PduSessionRelease.PcfRequestedReleaseReachesTheUeAndCompletes` | `test_amf_ngap_handover.cpp:120` and `test_amf_deregistration.cpp:116`, UDR |
| main, 44976336166 | 5 `AmfNgapTestGnb`, `AmfDeregistration.NormalDeregistrationTearsDownSessionAndUeContext`, 4 `Li*EndToEnd`, `SmfIntegration.FullSmContextLifecycleOverRealHttp2` | the UDR probe in all but one; `test_smf_pdu_session.cpp` `wait_reachable(..., 50)` on the SMF |
| main, 45014671318 | `UdmIntegration.SdmDataRetrievalAndSubscriptions`, `UdmIntegration.GenerateAuthDataFor5GAkaSubscriberProducesDistinctVectors` | `test_udm_uecm_sdm.cpp:267` `wait_reachable(..., 50)` on the UDR; see the second cause below |

All PR #56 attempts ran on `mastermind-073-small` (6.94 GB RAM, ~350 MB free during the run).
ctest runs one test at a time there, so two tests do not compete. What competes is the NFs of one
test: up to nine ASan NF binaries start at the same moment. Even unstripped plain builds of these
binaries are 150-400 MB each locally (NRF ~156 MB, AUSF ~307 MB, UDR ~399 MB, the largest). The CI
ASan sizes were not measured.

The bounds, read from the code. A refused connect returns at once, so the sleep sets each bound:

| Wait | Bound before |
|---|---|
| `wait_for_sbi_peers` in `test_amf_ngap_handover.cpp`, `test_amf_deregistration.cpp`, `test_li_amf_e2e.cpp` | 200 x 50 ms = ~10 s per peer |
| `wait_reachable(..., 50)` in `test_smf_pdu_session.cpp`, `test_udm_uecm_sdm.cpp`, `test_udm_ueau.cpp`, and the `pcf_up` loop in `test_smf_pdu_session.cpp` | 50 x 100 ms = ~5 s |
| `nf_test::wait_tcp_listening` (15 files) | 100 x 100 ms = ~10 s |

What the NF logs show. In PR #56 attempt 1 (`LiSmfEndToEnd...`), ctest started the test at
`12:00:24.30` UTC. The NRF was listening by `:24.97`. The UDR, spawned second, logged `starting` at
`:32.46` and `listening` at `:34.77`, but the test had given up and sent SIGTERM at `:34.74`. The
UDR needed ~10.3 s against a ~10 s bound, and ~8 s of that came before `main()` logged anything,
which is process load and ASan start-up. The paging explanation for those 8 s is an inference from
the free RAM and binary sizes. It was not measured. Every other NF the same test spawned after the
UDR was listening by `:31.8`.

None of these waits gives up early because an attempt returns at once. Each is bounded by its
sleeps, and an attempt that blocks (a half-up TLS listener) makes it longer, not shorter. Still, an
attempt count means a different bound for each kind of attempt, so the new helper takes a deadline.

A second cause, in `UdmIntegration.GenerateAuthDataFor5GAkaSubscriberProducesDistinctVectors` (run
45014671318). No wait gave up there. `wait_tcp_listening(7781)` passed. But the UDR listens first
and only then sends NFRegister. The test's request reached the UDM ~40 ms after the UDR's port
opened, the NRF refused the UDM's token (`invalid_scope: no registered UDR offers nudr-dr to a
UDM`), and the UDM answered 503. The wait checked the wrong condition.

A third effect, in `AmfNgapTestGnb.RegistrationAndPduSessionReachNsacf`. Once its wait failed,
`ASSERT_*` returned early from a test body that still had a joinable `std::thread`
(`receiver_thread`). `std::terminate` followed, and ctest reported "Subprocess aborted" in place of
the assertion that failed.

**Decision.**

1. `tests/integration/spawn_guard.hpp` gets one shared helper,
   `nf_test::wait_until_ready(probe, base_timeout, poll = 100 ms)`. It polls `probe` against a
   `steady_clock` deadline of `readiness_timeout(base_timeout)` and makes one last attempt at the
   deadline. `readiness_timeout_scale()` sets the scale:
   - the environment variable `TEST_READINESS_TIMEOUT_SCALE`, if it is a positive number (anything
     else is ignored, so a typo cannot set every wait to 0 s);
   - otherwise 5 for a ThreadSanitizer build and 3 for an AddressSanitizer build, detected at
     compile time through `__SANITIZE_THREAD__`/`__SANITIZE_ADDRESS__` (GCC) or `__has_feature`
     (Clang);
   - otherwise 1.
   A plain build keeps the bound it always had. A passing run is no slower, because the wait returns
   as soon as the probe succeeds.
2. `nf_test::wait_tcp_listening` is built on the helper. `max_attempts` keeps its meaning as a base
   of `max_attempts x 100 ms`, so its 15 callers get the margin with no edits.
3. The waits behind every failing test above now use the helper, with the same base bounds:
   `wait_for_sbi_peers` in the AMF handover, AMF deregistration/PDU session release and LI AMF/SMF
   files (10 s per peer), and `wait_reachable` plus the PCF probe in the SMF and the two UDM files
   (`max_attempts x 100 ms`, 5 s). With the ASan scale, the UDR case above has ~30 s against the
   ~10.3 s it needed.
4. In `test_udm_ueau.cpp`, each of the four tests now also waits, after the UDR's port opens, until
   the NRF issues a `nudr-dr` token towards a UDR (5 s base, scaled). The NRF issues one only after
   a UDR has registered. `test_udr_404_causes.cpp` and other UDR tests already get that token as the
   test AF. This is a readiness condition, not a retry of the behaviour under test: the
   generate-auth-data assertions are unchanged.
5. In `AmfNgapTestGnb.RegistrationAndPduSessionReachNsacf`, a scope guard stops and joins
   `receiver_thread`. An early `ASSERT_*` return then reports its own failure instead of aborting
   the process.

No assertion about NF behaviour changes. `nfs/` and `libs/` are not touched.

**Rejected alternatives.**

- *More CI retries (re-running failed tests or the job).* Retries hide the cause and add up to
  ~40 min per re-run of a sanitizer leg on a two-runner lab. Each of the three PR #56 attempts
  failed on a different subset of the same waits, so retries would not even pass reliably.
- *More RAM on the runner, or keeping sanitizer jobs off `mastermind-073-small`.* That is
  infrastructure, outside the test harness, and still leaves a bound that any loaded machine can
  exceed. The harness would remain wrong on a slow developer laptop.
- *Serialising tests further, or spawning NFs one at a time.* ctest is already serial. Waiting for
  each NF before spawning the next would add their start-up times together on every run, plain
  builds included, to fix a sanitizer-only margin.
- *Raising every base bound (e.g. 10 s to 30 s) for all builds.* This costs nothing on a passing
  run, but a plain build that hangs would take 3x longer to fail, and the bound would still say
  nothing about why sanitizer builds need more. The scale keeps plain builds as they were.
- *Rewriting all ~30 files' ad-hoc waits in this change.* That diff would be too broad to review as
  one subsystem change. Only the waits with CI evidence move now (see follow-ups).

**Disclosed.**

- Not run. The integration tests need `certs/`, which this change could not use, so nothing here
  was run against real NFs. What was verified: `clang-format-18 --dry-run -Werror` on the changed
  files; `-fsyntax-only` of the six changed TUs with their `compile_commands.json` flags; and a
  standalone probe of `spawn_guard.hpp` that showed scale 1 for a plain build, 3 under
  `-fsanitize=address` and 5 under `-fsanitize=thread` (GCC run; Clang checked by
  `static_assert`), that `TEST_READINESS_TIMEOUT_SCALE=0.5` overrides, that a non-numeric value is
  ignored, and that a refused port returns false. Whether the new margins are enough on
  `mastermind-073-small` will only show on the next `sanitize` runs.
- The 3x/5x factors are a judgement: 3x the observed worst case for ASan, more for TSan's larger
  slowdown. They were not tuned from a distribution of start-up times.
- `wait_for_sbi_peers` still gives each peer its own budget, as before. If an NF never starts, the
  test fails only after (peers x scaled bound), which ctest's `--timeout` caps. NFs start
  concurrently, so a passing run is unaffected.
- An ASan SEGV on a null OpenSSL rwlock, logged right after such a failure, is not fixed here.
  It was reached through libcurl `ERR_error_string_n` in run 45014671318 and through `OBJ_sn2nid`
  in PR #56 attempt 3. It looks like a teardown race in whichever process was exiting. Which
  process that was is not established.

**Follow-ups (not in this change).**

- `wait_for_upf_sx_association` (`test_amf_ngap_handover.cpp`, `test_li_amf_e2e.cpp`;
  40 x 250 ms) creates an SM context on each attempt, so it is not a pure readiness probe. It was
  left alone. No CI evidence shows it giving up.
- The LI files' `post_x1` and `served` loops (100 x 100 ms), the NSACF report loop
  (60 x 100 ms), and the attempt-count waits in the other ~25 integration files should move onto
  `nf_test::wait_until_ready` when one of them shows up in CI.
- `test_udm_uecm_sdm.cpp` has the same listen-before-register order as `test_udm_ueau.cpp`. It
  spawns the UDM only after the UDR, which makes the race narrower, and CI has not shown it there.
  It can adopt the same NRF-registration wait if it does.
