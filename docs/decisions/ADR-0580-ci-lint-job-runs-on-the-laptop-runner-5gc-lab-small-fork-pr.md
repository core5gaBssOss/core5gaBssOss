## ADR-0580: CI lint job runs on the laptop runner (5gc-lab-small); fork-PR approval for all external contributors

**Date:** 2026-11-16. **Status:** Proposed.

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
2. The repository's fork-PR workflow approval policy is now `all_external_contributors`. The user approved
   this, and it was applied on 2026-11-16 through
   `PUT /repos/{owner}/{repo}/actions/permissions/fork-pr-contributor-approval`. This is repo
   configuration, not code, and this ADR records it.

**Disclosed limitations.**
- The laptop's vcpkg binary cache and ccache under `~/.cache/5gc-ci` start cold. The first `lint` that
  needs to configure will build every vcpkg port from source. With about 6.9 GiB RAM, that run can spill into
  swap and be much slower than on the lab host. Later runs reuse the cache.
- `$HOME/.local/build-tools-venv` does not exist on the laptop. The job only adds it to PATH, and every
  tool resolves from `/usr/bin`, so this is not expected to fail. If an asn1c or codegen step turns out to
  need that venv, the fix is to provision it there.
- `lint` now depends on the laptop being powered on and online. If it is offline, `lint` queues until it
  returns, or until the workflow is reverted to `5gc-lab`. There is no automatic fallback between labels.
- The weekly full sweep (4.5-5.5 h on the lab host, ADR-0371) and its 360-minute cap have not been measured
  on the laptop. On a smaller box the sweep could hit that cap.

**Rejected alternatives.**
- *Also move `docs-lint.yml` (markdownlint, readme-append-only) to the laptop.* The user asked for this.
  It was not done, because those jobs already run on GitHub-hosted `ubuntu-latest`, take about 15 s, and
  never use the lab host. Moving them would add load and fork-PR exposure to the laptop and would free no
  self-hosted capacity. This can be revisited if the user still wants it.
- *Register the laptop with the `5gc-lab` label so it shares all jobs.* Rejected because `build` and
  `sanitize` need the lab host's warm caches and memory headroom (ADR-0363). With about 6.9 GiB RAM, a
  sanitizer build would be likely to OOM.
- *Leave the fork-PR approval policy at `first_time_contributors`.* Rejected because, with two self-hosted
  runners on a public repo, any returning contributor's fork could execute code on the user's machines.
