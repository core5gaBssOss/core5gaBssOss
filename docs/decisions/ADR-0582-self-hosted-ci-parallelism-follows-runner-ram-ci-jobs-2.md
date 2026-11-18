## ADR-0582: Self-hosted CI parallelism follows runner RAM: CI_JOBS 2 under 12 GiB, else 4

**Date:** 2026-11-17. **Status:** Accepted (user, 2026-11-18).

**Context.** ADR-0581 put the laptop runner `mastermind-073-small` (about 7 GB RAM, 8 cores) into the
`5gc-lab` pool alongside the lab host `5gc-lab-host` (15 GB). The self-hosted branch of every job's "Locate
caches and tools" step hard-coded `CI_JOBS=4` and `VCPKG_MAX_CONCURRENCY=4`, which were tuned for the lab
host (ADR-0363).

On PR #25 (2026-11-16) the laptop ran `sanitize (tsan)` in 145 minutes, against about 48 on the lab host.
The build step alone took 99 minutes. The laptop session's memory watcher, sampling every 30 s for the
first 43 minutes, measured a minimum of 149 MB available RAM and a peak of 15.2 GB swap. In 71% of samples
less than 500 MB was available. There were no OOM kills. 3 or 4 parallel `cc1plus` processes each used
1.3-2.2 GB. The test step (41 min) was not memory-bound in the same way. The plain `build` there took
43 minutes, comparable to the lab host.

The user chose to keep the laptop fully shared at 2 parallel compiles, rather than restricting `sanitize`
to the lab host.

**Decision.** In all three self-hosted preflight blocks in `ci.yml` (`build`, `sanitize`, `lint`), the job
reads `MemTotal` from `/proc/meminfo`. Below 12 GiB it sets `CI_JOBS=2` and `VCPKG_MAX_CONCURRENCY=2`;
otherwise both stay at 4. The job logs the value it picked. The rule keys on the machine's RAM, not on a
runner name, so a future runner gets a sensible value without a workflow change. For reference,
`MemTotal` reads 14 GiB on the lab host (giving 4) and 6 GiB on the laptop (giving 2).

The existing `asan-ubsan` `-j1` fallback (ADR-0222) only applies when `CI_JOBS` is unset, as on
GitHub-hosted runners. It is unchanged.

**Disclosed limitations.**
- Not yet measured. The benefit (less swapping, a shorter sanitizer build on the laptop) is expected but
  unverified until the first laptop sanitize run at -j2. A plain laptop `build` may get slower, because
  at -j4 it was not badly swap-bound (43 min).
- The 12 GiB threshold is a judgment between the two machines in hand, not a measured crossover.
- `lint`'s clang-tidy `-P` also drops to 2 on the laptop, so a large changed-file lint takes longer there.
- `fuzz-nightly.yml` (PR #26, not yet on `main`) has its own preflight. This change does not cover it.

**Rejected alternatives.**
- *Restrict `sanitize` to the lab host through a label only it has.* This was recommended, and the user
  declined it in favour of keeping the laptop fully shared.
- *A per-runner override file (for example `~/.config/5gc-ci/env`).* Rejected for now: it adds
  configuration that lives outside the repo on each machine, and a RAM rule covers both runners. It is still
  possible later if a machine needs an exception.
- *Key on the runner name (`$RUNNER_NAME`).* Rejected: it hard-codes a machine identity into the workflow
  and breaks silently if the runner is re-registered under another name.

**Measured (2026-11-17, laptop, -j2).** These are measured times, not the expectation in *Disclosed*:
- PR #30: cold `sanitize (asan-ubsan)` took 186 min (build step 131 min, tests 50 min).
- PR #26 run 44106505091: warm `build` took 45 min and warm `asan-ubsan` 66 min. The lab host takes about 55 min for asan-ubsan at -j4.
- There were no OOM kills.

The *Disclosed* item about `fuzz-nightly.yml` is closed. PR #26 applied the same rule there before it merged.
