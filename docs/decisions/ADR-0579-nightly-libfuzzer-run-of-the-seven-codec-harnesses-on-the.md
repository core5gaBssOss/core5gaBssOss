## ADR-0579: Nightly libFuzzer run of the seven codec harnesses on the lab runner (ADR-0572 v0.2.0 item 5, fuzz part)

**Date:** 2026-11-15. **Status:** Accepted (user, 2026-11-18). Proposed (user, 2026-11-15: "yes start it", item 5). Item 5 of the
v0.2.0 plan (ADR-0572) has two parts; this ADR is the first, the nightly fuzz workflow. The recorded
UERANSIM run is a separate increment (CLAUDE.md: one subsystem per turn).

**Context.** `tests/fuzz/` holds seven libFuzzer harnesses (Diameter, TCAP message, TCAP dialogue
portion, MAP, CAP, SS7 transport, LI X2/X3) behind `5GC_ENABLE_FUZZING=ON`. No workflow ever built or
ran them (ADR-0572 B9: "libFuzzer targets never run in CI"); `tests/fuzz/CMakeLists.txt` says they are
"run manually or from a dedicated, time-boxed CI job".

**Decision:**

- New workflow `.github/workflows/fuzz-nightly.yml`: cron `30 1 * * *` and `workflow_dispatch`
  (`seconds_per_target`, default 120, accepted range 10..1800). It is not part of `ci.yml`, so no PR
  waits for it.
- Same self-hosted lab runner and cache layout as `ci.yml` (ADR-0363), a separate concurrency group
  `fuzz-nightly` that never cancels a running night.
- **Clang 22, not 18.** libFuzzer needs the compiler-rt runtime. On the lab machine `clang-18` has
  none (`libclang_rt.fuzzer-x86_64.a` and the ASan runtime are missing: the link of `fuzz_diameter`
  failed with exactly that), `clang-22` has it (`libclang-rt-22-dev`). The lint job already uses
  clang-tidy-22.
- Builds only the seven fuzz targets in `build-fuzz/`, then runs each for `-max_total_time=<budget>`
  with `-timeout=25`, wrapped in `timeout <budget>+120` so a hang outside libFuzzer's own timeout
  (the sandbox hang recorded in ADR-0065) cannot hold the one runner.
- One corpus directory per target under `~/.cache/5gc-ci/fuzz-corpus/`, so a night starts from what
  earlier nights found. A crash, leak, OOM or timeout fails the job after every target had its turn,
  and the `crash-*`/`oom-*`/`timeout-*` files are uploaded as an artifact (30 days).

**Rejected alternatives.**

- **Fuzz on every PR.** The single runner already spends ~2.5 h per PR run (ADR-0572 B10).
- **A GitHub-hosted runner.** Would need clang-22 and the whole vcpkg set from scratch on a 2-vCPU
  machine; ADR-0363 moved CI off those for that reason.
- **`ASAN_OPTIONS=symbolize=0` as in `tests/fuzz/CMakeLists.txt`.** It hides stack frames in a crash
  report, and the hang it works around was seen in a sandbox. Not set; the `timeout` wrapper covers
  the risk instead.

**Disclosed.**

- **The workflow itself has not run on the runner.** What was run here: a clang-22 build of the seven
  targets (`build-fuzz/`, ~25 min cold on this machine, 1165 ninja steps because `li_core` pulls in
  the generated LI library) and each target for 10 s with the workflow's flags. All seven exited 0,
  no crash files. Coverage after 10 s: `fuzz_diameter` cov 42, `fuzz_tcap_message` 97,
  `fuzz_dialogue_portion` 11 (corpus of one input: the harness saturates at once, so it adds little
  as it stands), `fuzz_map_operations` 26, `fuzz_cap_operations` 27, `fuzz_ss7_transport` 32,
  `fuzz_li_x2x3` 39. That is a smoke run, not evidence of absence of bugs.
- **No seed corpus is committed**; the first night starts empty.
- **Not fuzzed, deferred for future implementation:** PFCP (`libs/pfcp-core`), NGAP
  (`libs/ngap-generated`), and the SBI JSON decoders.
- The nightly shares the one runner: a PR's CI that arrives during it waits for it (about 15 minutes
  at the default budget plus the build, which is ccache-warm after the first night).
- Since ADR-0581 the `5gc-lab` label is shared by two runners, so the nightly can land on the ~7 GB
  laptop runner. The build's parallelism follows the ADR-0582 RAM rule: 2 under 12 GiB, else 4. The
  rule replaces the hard-coded `-j4` and `VCPKG_MAX_CONCURRENCY=4`. The fuzz build is ASan-instrumented,
  so each compile is expected to take about 2.3-2.8 GB, judging from the asan-ubsan legs. That figure is
  inferred and has not been measured for the fuzz targets. A cold fuzz build on the laptop is expected
  to be slow, much as its cold asan-ubsan build took 131 min.
- No actionlint on this machine; the YAML was parsed with PyYAML only. The first scheduled or
  manual run is the real test.

**Verified.** Local only, as above. CI/runner result to be added after the first `workflow_dispatch`.

**Update (2026-11-18).** Merged to `main` in #26. The first `workflow_dispatch` run, 44230521237 on
`main` 3d02404f (2026-11-18, 03:27-03:43 UTC, 16 min including the build), succeeded. Each of the
seven harnesses ran for its 120 s budget. They executed 21.4M, 30.1M, 32.8M, 41.2M, 58.2M, 77.2M
and 94.0M inputs (`Done N runs in 121 second(s)`), and none crashed. This is a time-boxed run with no
coverage measured, not a proof that the codecs are free of bugs.
