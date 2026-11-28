## ADR-0596: Runner topology: MASTERMIND-007 and mastermind-073 carry all self-hosted CI; mastermindAI is the master machine, fallback only

**Date:** 2026-11-20. **Status:** Accepted; Decision and Fallback rule superseded by ADR-0643 (2026-11-28).
Original acceptance (user, 2026-11-20: "5gc-lab-mastermindAI: Is going to be main
master machine. Below both needs to be runners (load them maximum), if any jobs cannot be done by them
move to mastermindAI").

**Context.** On 2026-11-20 a new laptop, `mastermindAI` (24 cores, 30 GiB RAM), was set up with the
migration script and registered as a third self-hosted runner, `5gc-lab-mastermindAI`. That script gave it
the same labels as the other two (`5gc-lab`, `5gc-lab-small`), so GitHub could hand any CI job to any of
the three machines. The user made `mastermindAI` the development (master) machine. The two existing
machines are to be the CI runners and should be loaded as fully as possible.

| Runner | Machine | RAM | `CI_JOBS` (ADR-0582) | Role |
|---|---|---|---|---|
| `5gc-lab-host` | `MASTERMIND-007` | 14 GiB | 4 | CI runner |
| `mastermind-073-small` | `mastermind-073` | 6 GiB | 2 | CI runner |
| `5gc-lab-mastermindAI` | `mastermindAI` | 30 GiB | 4 | master machine; fallback runner |

**Decision.**
- `5gc-lab-host` and `mastermind-073-small` keep the labels `5gc-lab` and `5gc-lab-small`. Every
  self-hosted `runs-on` in `.github/workflows/` (3x `5gc-lab`, 3x `5gc-lab-small`) therefore resolves only
  to these two runners. No workflow file changes.
- `5gc-lab-mastermindAI` carries only the custom label `5gc-master` (set 2026-11-20 through
  `PUT /repos/{owner}/{repo}/actions/runners/{id}/labels`). No workflow targets `5gc-master` today, so it
  receives no jobs. It stays registered and online, as the fallback.
- **Fallback rule.** A job goes to `5gc-master` only when it is shown, by a recorded failure, that the two
  runners cannot run it (for example, a job that is OOM-killed on both, or one needing a tool or
  capability they lack). That move is a `runs-on` change in its own PR with its own ADR, citing the failure.
  Being slow or queued is not a reason: "load them maximum" means queueing on the two runners is accepted.
- **"Maximum load"** means both runners take every self-hosted job, at the parallelism ADR-0582 sets from
  their RAM. It does not raise ADR-0582's numbers: the 6 GiB laptop at more than 2 jobs is the
  swap/OOM case ADR-0582 was written for.

**Incident during the change (2026-11-20).** Right after the `PUT` on `5gc-lab-mastermindAI`, a listing
showed `5gc-lab-host` and `mastermind-073-small` without the `5gc-lab` label (only `5gc-lab-small` left).
The cause is not known; only the one runner's labels were written. The label was re-added to both at once
(`POST .../runners/{id}/labels`), before any job was queued, and the final listing was checked. Anyone
changing runner labels again should list all runners' labels afterwards.

**Disclosed limitations.**
- **No automatic overflow.** GitHub Actions has no runner priority or spill-over: a job waits for a runner
  with matching labels, however long. If both runners are offline, self-hosted CI stalls; it does not move
  to `mastermindAI`. Re-adding `5gc-lab` to `5gc-lab-mastermindAI` (one API call) is the manual escape.
- **One job per runner.** Each machine runs one runner process, so at most two self-hosted jobs run at a
  time. A second runner process on `MASTERMIND-007` was not considered, since it would double its memory
  load past what ADR-0582 allows.
- **Measured fact behind the topology:** on 2026-11-20 the migration script's local build of `main`
  (`-j24`) on `mastermindAI` used all 30 GiB RAM and 44 GiB swap. That is a local-build observation, not a
  CI one.
- ADR-0581's *Rollback* line and the host list in ADR-0572 B10 now describe three machines, not two.

**Rejected alternatives.**
- *Keep all three runners in the pool (the migration script default).* Rejected by the user: the master
  machine is for development, and CI jobs on it compete with local builds (`ci-runner-shares-host`).
- *Deregister `5gc-lab-mastermindAI`.* Rejected: the user wants it available as a fallback, and
  re-registering takes a token and the runner setup each time. Relabelling is one API call each way.
- *Make the master the target for the heavy legs (`sanitize`) now.* Rejected: no job has been shown to
  fail on the two runners, and the user asked that they be loaded first.
