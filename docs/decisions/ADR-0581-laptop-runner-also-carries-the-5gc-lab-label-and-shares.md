## ADR-0581: Laptop runner also carries the 5gc-lab label and shares build, sanitize and fuzz jobs

**Date:** 2026-11-16. **Status:** Proposed.

**Context.** ADR-0580 moved `lint` and `docs-lint` to the laptop runner `mastermind-073-small` (label
`5gc-lab-small`). Those jobs take seconds, so the laptop sat idle while the lab host `5gc-lab-host`
worked through a queue of hours. On 2026-11-16 that queue was the PR #26 sanitizers, then the `main`
build and its sanitizers. The user directed that the laptop be used 100%.

**Decision.** The runner `mastermind-073-small` now also carries the label `5gc-lab`. This was added on
2026-11-16 through `POST /repos/{owner}/{repo}/actions/runners/{id}/labels`; it is runner configuration,
and no workflow file changes. Every job with `runs-on: [self-hosted, linux, x64, 5gc-lab]` (`build`,
`sanitize`, `fuzz-nightly`) now goes to whichever of the two runners is free. `lint` and `docs-lint` stay
on `5gc-lab-small`, so only the laptop runs them.

Prerequisite done before this change: the user approved creating `~/.local/build-tools-venv` on the
laptop (Python 3.14, jinja2 3.1.6, PyYAML 6.0.3, from `tools/sbi-codegen/requirements.txt`). This
removes the PEP 668 `pip install` failure that ADR-0580 disclosed.

**Incident and fix (2026-11-16).** The label was first added before any workflow change. Within
minutes, the laptop picked up a `sanitize (asan-ubsan)` job and a `build` job, and both failed in under
a minute at "Locate caches and tools for this runner (ADR-0363)". That preflight in `build` and
`sanitize` still listed `clang-tidy-18`, which was left over from before ADR-0482. The laptop has only
`clang-tidy-22`. Neither job ever invokes clang-tidy (only `lint` does, and it checks `clang-tidy-22`), so
this PR drops `clang-tidy-18` from those two preflight lists rather than installing an unused package. The
label was removed again at once, to stop more fast failures, and it is re-added only after this fix is on
`main`.

**Disclosed limitations.**
- **Memory.** The laptop has about 6.9 GiB RAM, against 15 GB on the lab host. The self-hosted branch of
  each job still uses `CI_JOBS=4` / `VCPKG_MAX_CONCURRENCY=4`, a value tuned for the lab host
  (ADR-0363). On the laptop, a `sanitize` leg (ASan/TSan instrumented build plus ctest) may push into swap
  or be OOM-killed. This has not been measured.
- **Cold caches.** The laptop's `~/.cache/5gc-ci` vcpkg binary cache and ccache start empty. Its first
  `build` compiles every vcpkg port. That run has not been timed, and it could approach the 240-minute job cap.
- **Toolchain drift.** The desktop's venv provides cmake 4.4.0, while the laptop uses the system cmake
  4.2.3. Each runner keeps its own vcpkg checkout at the baseline pinned in `vcpkg.json`. A result that
  differs between the two runners is a runner-environment bug and should be investigated as one.
- **No placement control.** GitHub assigns each job to any free matching runner. A heavy `sanitize` leg can
  land on the laptop even while the lab host is idle.
- **Shared host rule.** `ci-runner-shares-host` (user memory) applies to both machines now. Do not run local
  integration tests on the laptop while it is running a CI job.
- **Rollback.** Removing the label takes one API call:
  `DELETE /repos/{owner}/{repo}/actions/runners/{id}/labels/5gc-lab`.

**Rejected alternatives.**
- *Move only `build` to the laptop through a workflow `runs-on` change.* Rejected because it leaves the lab
  host idle while the laptop queues builds. Sharing the label lets the load balance itself.
- *Lower `CI_JOBS` on the laptop now, ahead of measurement.* Deferred: the self-hosted branch has no
  per-runner knob yet. Adding one (for example, reading a runner-local env file) is a follow-up, to do if
  the first laptop `sanitize` leg shows memory pressure.
- *Keep the ADR-0580 split (laptop runs only lint and docs).* Rejected by the user ("use the laptop 100%").
