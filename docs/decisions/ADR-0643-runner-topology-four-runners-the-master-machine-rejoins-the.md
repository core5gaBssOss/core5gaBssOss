## ADR-0643: Runner topology: four runners, the master machine rejoins the CI pool

**Date:** 2026-11-28. **Status:** Accepted (user, 2026-11-27: "make use of" all laptops; 2026-11-28: the queue was too long for two runners).
It supersedes the *Decision* and *Fallback rule* of ADR-0596.

**Context.** ADR-0596 (2026-11-20) kept the master machine `mastermindAI` out of CI: its runner
`5gc-lab-mastermindAI` carried only `5gc-master`, which no workflow targets. Only `5gc-lab-host`
(`MASTERMIND-007`) and `mastermind-073-small` (`mastermind-073`) took self-hosted jobs, and a job could
move to the master only by a `runs-on` PR citing a recorded failure on both.

History since ADR-0596 (from `docs/project-context/host-toolchain.md`):
- 2026-11-21 to 2026-11-22: runner 23 briefly had CI labels. They were removed after port contention
  with local tests on the same machine (spawned NFs use fixed ports).
- 2026-11-23: the user removed `5gc-lab` from `mastermind-073-small` after its ASan jobs repeatedly
  failed, when spawned NFs took ~30 s to start (PRs #56, #58, #61, #60). ADR-0617 then scaled the
  readiness waits 3x under ASan and 5x under TSan. From then until 2026-11-26, `5gc-lab-host` ran every
  build, sanitize and fuzz job alone.
- 2026-11-26: `5gc-lab` was re-added to runner 22 at the user's request (`POST .../runners/22/labels`).
- 2026-11-27 (user: "make use of" all laptops): runner 23 got `5gc-lab` + `5gc-lab-small` again beside
  `5gc-master`, and a fourth runner, `5gc-lab-mastermindAI-small` (id 24), appeared with
  `5gc-lab-small`.

Two runners could not keep up with the PR volume of SBI-03 (one PR per NF). Observed through
`GET /repos/{owner}/{repo}/actions/runners` and `gh run list`:
- Run 45381072534 (`fix/bss-provisioning-tmf-error`) was created on 2026-11-24, and its
  `sanitize (tsan)` job finished on 2026-11-26 at 11:14 UTC.
- On 2026-11-26 `mastermind-073-small` was offline until about 17:20 UTC.
- On 2026-11-27 at 17:39 UTC, after the change, 15 runs were queued (11 `CI`, 4 `Docs lint`) and 3 were
  in progress.

The labels on runners 23 and 24 were set outside a PR, before this ADR. This ADR records them after the
fact.

**Decision:** All four self-hosted runners take CI jobs: the master's runner rejoins the pool, and a second runner on the master takes only light jobs.

| Runner (id) | Machine | RAM | Labels | Takes |
|---|---|---|---|---|
| `5gc-lab-host` (2) | `MASTERMIND-007` | 14 GiB | `5gc-lab`, `5gc-lab-small` | every self-hosted job |
| `mastermind-073-small` (22) | `mastermind-073` | 6 GiB | `5gc-lab`, `5gc-lab-small` | every self-hosted job |
| `5gc-lab-mastermindAI` (23) | `mastermindAI` | 30 GiB | `5gc-lab`, `5gc-lab-small`, `5gc-master` | every self-hosted job |
| `5gc-lab-mastermindAI-small` (24) | `mastermindAI` | (shared with id 23) | `5gc-lab-small` | light jobs only |

- **Id 23 rejoins the pool.** It gets back `5gc-lab` and `5gc-lab-small`, so the `runs-on` targets in
  `.github/workflows/` (3x `5gc-lab`: `ci.yml` build and sanitize, `fuzz-nightly.yml`; 3x
  `5gc-lab-small`: `ci.yml` lint, both `docs-lint.yml` jobs) resolve to it as well. `5gc-master` stays,
  unused, so a later return to fallback-only is one label call.
- **Id 24 is a second runner process on the master, for light jobs.** It carries only `5gc-lab-small`
  (lint, markdownlint, readme-append-only), so those short jobs do not wait behind a 50-minute sanitize
  job on id 23. It never takes `5gc-lab` jobs.
- **No guard for local development on the master** (user's choice). CI jobs on `mastermindAI` share the
  machine with local builds; `ci-runner-shares-host` applies to the master as it does to the other two.
  Spawned NFs use fixed ports, so local integration tests on `mastermindAI` run only while runner 23 is
  idle (the 2026-11-21/22 port contention). CI's service containers use offset host ports (15434-15439,
  16379, 19092, 18030/19030/18040) and do not collide with the machine's own containers.
- **No workflow file changes.** Build parallelism still follows ADR-0582 (`CI_JOBS` from runner RAM).
- **Superseded rule.** ADR-0596's fallback rule (move to the master only by a `runs-on` PR citing a
  recorded failure) and its reading of "load them maximum" as "queueing on the two runners is accepted"
  no longer apply. Its label-incident note, *No automatic overflow* and the ADR-0582 limits still stand.

**Evidence that the master runs the heavy legs.** Run 45945186355 (`fix/bss-balance-tmf-error`):
`sanitize (tsan)` passed on `5gc-lab-mastermindAI`. On 2026-11-27 17:39 UTC, id 23 was running
`sanitize (tsan)` for run 45945178985 and id 24 was running `lint` for run 46041402625.

**Disclosed limitations.**
- **The change came before the ADR.** Labels on ids 23 and 24 were set without a PR. Neither the
  exact time nor how runner 24 was registered is recorded. This ADR documents the labels as observed on
  2026-11-27 and 2026-11-28.
- **Two processes, one RAM budget.** ADR-0582 sizes `CI_JOBS` from the machine's total RAM and does not
  know a second runner shares it. On `mastermindAI` a heavy job (id 23) and a lint job (id 24) can run
  at once, alongside local development. Memory pressure from that has not been measured. Lint is short
  and light, so it is not expected to matter, but this is unverified.
- **Labels set through the API are lost on re-registration.** Re-registering any runner with
  `config.sh` must pass the labels in the table above (`--labels ...`). After any label change, list
  all four runners' labels (the ADR-0596 relabel incident).
- **The laptop's ASan risk is back.** Runner 22 takes `5gc-lab` jobs again, which is the 2026-11-23
  failure case. ADR-0617's scaled readiness waits are the mitigation. Whether they are enough on the
  laptop has not been measured since the re-add.
- **Dev and CI share the master.** A heavy local build (for example the `-j24` migration build that used
  all 30 GiB RAM and 44 GiB swap, ADR-0596) running alongside a sanitize job can OOM one or the other.
  Accepted by the user; the escape is removing `5gc-lab` from id 23 (one API call).

**Rejected alternatives.**
- *Keep ADR-0596 (master as fallback only).* Rejected: with two runners, one of them a 6 GiB laptop that
  is sometimes offline, runs queued for up to two days.
- *Pause the master's CI while developing (drop `5gc-lab` from id 23 during heavy local builds).*
  Rejected by the user as a standing rule. It stays available as a manual escape.
- *Give id 24 `5gc-lab` as well.* Not proposed (this ADR's reasoning, not a user decision): two heavy builds at once on one machine would double the RAM
  that ADR-0582 budgets for.
- *Add GitHub-hosted runners for the heavy legs.* Not proposed (this ADR's reasoning, not a user decision): the self-hosted cache (`~/.cache/5gc-ci`)
  is on the self-hosted machines, and the master machine was idle capacity.
