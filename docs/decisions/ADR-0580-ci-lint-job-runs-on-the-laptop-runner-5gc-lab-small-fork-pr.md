## ADR-0580: CI lint job runs on the laptop runner (5gc-lab-small); fork-PR approval for all external contributors

**Date:** 2026-11-16. **Status:** Accepted (user, 2026-11-18).

**Context.** Until now, every self-hosted CI job (`build`, `sanitize`, `lint` in `ci.yml`, and `fuzz` in
`fuzz-nightly.yml`) has run on the single lab runner `5gc-lab-host` (label `5gc-lab`, ADR-0363). Jobs
queue behind each other there. On 2026-11-16, for example, the `main` push CI waited behind PR #26's build.
That same day the user registered a second self-hosted runner on a laptop: `mastermind-073-small`,
label `5gc-lab-small`, runner 2.338.2, Ubuntu 26.04, 8 cores, about 6.9 GiB RAM.

The laptop was checked read-only before this change, and it has every tool that `lint`'s self-hosted
preflight requires (ninja, cmake, clang-18, clang-format-18, clang-tidy-22, bison, flex, ccache,
docker), plus libsctp-dev, libbpf-dev and libcap-dev.

The repository is public. A self-hosted runner on a public repo will execute fork-PR workflows. A fork
PR can set its own `runs-on` to either self-hosted label. The fork-PR approval policy had been
`first_time_contributors`, which let any previously merged contributor's fork PR run on either machine
unattended.

**Decision.**
1. `ci.yml` `lint` now uses `runs-on: [self-hosted, linux, x64, 5gc-lab-small]`. `build` and `sanitize`
   stay on `5gc-lab`, and so does `fuzz-nightly`. The job body is unchanged. The existing self-hosted
   branch of "Locate caches and tools" already caps the job at `CI_JOBS=4` and `VCPKG_MAX_CONCURRENCY=4`.
2. `docs-lint.yml` (`markdownlint`, `readme-append-only`) also moves to `5gc-lab-small`. This was
   user-directed on 2026-11-16. Before this, both jobs ran on GitHub-hosted `ubuntu-latest` (about 15 s)
   and never used the lab host, so this move frees no lab-host capacity. That trade-off was raised with
   the user before the change. The file's header previously said "never the self-hosted one"; it now
   says never the lab host.
3. The repository's fork-PR workflow approval policy is now `all_external_contributors`. The user approved
   this, and it was applied on 2026-11-16 through
   `PUT /repos/{owner}/{repo}/actions/permissions/fork-pr-contributor-approval`. This is repo
   configuration, not code, and this ADR records it.

**Disclosed limitations.**
- The laptop's vcpkg binary cache and ccache under `~/.cache/5gc-ci` start cold. The first `lint` that
  needs to configure will build every vcpkg port from source. With about 6.9 GiB RAM, that run can spill into
  swap and be much slower than on the lab host. Later runs reuse the cache.
- `$HOME/.local/build-tools-venv` does not exist on the laptop. On the lab host, `pip` resolves from that
  venv. On the laptop, `pip` falls back to the system Python, and Ubuntu 26.04 marks that Python
  EXTERNALLY-MANAGED (PEP 668). So the first `lint` that lints a `.cpp` will most likely fail at "Install
  sbi-codegen Python deps". This has not been observed yet: PR #28's lint skipped that step. The fix is a
  one-time `python3 -m venv ~/.local/build-tools-venv` on the laptop, followed by installing
  `tools/sbi-codegen/requirements.txt` into it. That step is pending the user's go-ahead.
- `lint` and both docs-lint jobs now depend on the laptop being powered on and online. If it is offline, they queue until it
  returns, or until the workflow is reverted to `5gc-lab`. There is no automatic fallback between labels.
- The weekly full sweep (4.5-5.5 h on the lab host, ADR-0371) and its 360-minute cap have not been measured
  on the laptop. On a smaller box the sweep could hit that cap.

**Rejected alternatives.**
- *Register the laptop with the `5gc-lab` label so it shares all jobs.* Rejected because `build` and
  `sanitize` need the lab host's warm caches and memory headroom (ADR-0363). With about 6.9 GiB RAM, a
  sanitizer build would be likely to OOM.
- *Leave the fork-PR approval policy at `first_time_contributors`.* Rejected because, with two self-hosted
  runners on a public repo, any returning contributor's fork could execute code on the user's machines.
