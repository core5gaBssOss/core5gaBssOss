## ADR-0509: Release, ownership and repository-security process: CHANGELOG and SemVer, CODEOWNERS and area labels, Dependabot, CodeQL, Scorecard, main ruleset

**Date:** 2026-10-22. **Status:** Accepted.

**Context.** Phase 3 of the professional-setup review (Phases 1 and 2 are ADR-0507 and ADR-0508)
covers process. On 2026-10-22 the repository had:
- no tag, no release and no changelog;
- no `CODEOWNERS`, and only GitHub's default labels;
- no protection on `main` (a ruleset existed, but it was disabled and targeted no branch);
- no Dependabot, code scanning or private vulnerability reporting.

Secret scanning with push protection was already on. ADR-0467 had planned `CODEOWNERS`, a path
labeler, a check that rejects multi-area PRs, and branch protection, and left all of them undone.

**Decision:**
- **Releases.**
  - `CHANGELOG.md` in Keep a Changelog format, versioned with SemVer.
  - `docs/RELEASING.md` gives the steps. The version must agree in `CMakeLists.txt`, `vcpkg.json`
    and the tag.
  - The first release is **v0.1.0**, the version both files already carry. The tag is cut after the
    open PRs merge, so it includes them.
- **Ownership and labels.**
  - `.github/CODEOWNERS` and `.github/labeler.yml` both mirror the area table in
    `.github/CONTRIBUTING.md`, which stays the source of truth.
  - Labels created: nine `area/*` labels, `cross-area`, `size/XS` to `size/XL` and `dependencies`.
  - `.github/workflows/pr-labels.yml` labels every PR by area and size.
    - It fails a PR that touches more than one area, unless the PR carries `cross-area`.
    - The failure applies only to authors without write access; for the maintainer and bots it is
      a warning.
    - It runs as `pull_request_target` on a GitHub-hosted runner and never checks out PR code.
- **Dependencies.** `.github/dependabot.yml` covers:
  - GitHub Actions;
  - npm for `gui/web`;
  - pip for the CHF and NWDAF training sidecars and `tools/sbi-codegen`.

  Updates are monthly, grouped, and one directory per entry, so that no update PR spans two areas.
  Dependabot alerts and security updates are enabled.
- **Code scanning.** CodeQL default setup is enabled for actions, C/C++, JavaScript/TypeScript and
  Python, on GitHub-hosted runners. **OpenSSF Scorecard** (`.github/workflows/scorecard.yml`)
  publishes results for the README badge and uploads them to code scanning.
- **Private vulnerability reporting** is enabled, and `SECURITY.md` names it as the preferred
  channel.
- **Ruleset "Protect main"** is active on the default branch. It blocks deletion and force-push and
  requires linear history. It has no bypass actors. Merge-commit merges are disabled in the
  repository settings to match. Head branches are deleted automatically after a merge.
- **Markdown lint.** `.github/workflows/docs-lint.yml` runs markdownlint-cli2 on the front-page
  documents listed in `.markdownlint-cli2.jsonc`. Line length, inline HTML, first-line heading and
  table-pipe spacing are off, because of how the repository already writes Markdown. Four files were
  fixed to pass.
- **New workflows pin actions by commit SHA**, with the version in a comment. Dependabot keeps
  those pins current. `ci.yml` keeps its existing tag pins.

**Rejected alternatives.**
- *Required status checks and required reviews on `main`.* CLAUDE.md records "no required-status-check
  branch protection, by design". CI takes about 40 minutes per build on the single self-hosted
  runner, so a required check would hold every merge behind the queue. GitHub does not let an author
  approve their own PR, so a required review would block a single maintainer. Revisit once a second
  maintainer or a faster runner exists.
- *A maintainer bypass on the ruleset.* With one maintainer, a bypass would leave the ruleset
  protecting nothing. A history rewrite like ADR-0507's now needs the ruleset disabled first, as a
  deliberate step.
- *A DCO sign-off check.* ADR-0467 chose a CLA, and the user deferred sign-off (2026-10-22).
- *Dependabot for vcpkg and Docker.* Dependabot does not support vcpkg. Docker base-image updates
  across the Dockerfiles would each rebuild the lab images, so they wait until the images are built
  in CI.
- *A coverage badge.* No coverage report exists. It needs a coverage build first.
- *yamllint.* Helm templates are not valid YAML. The workflows are better checked by actionlint,
  which this ADR's workflows were run through.

**Verified (2026-10-22).**
- markdownlint-cli2 0.23.2, the version the action uses, run locally: 0 issues in 8 files.
- actionlint 1.7.7 on the three new workflows: no findings.
- The release-notes `awk` in `docs/RELEASING.md` extracts the 0.1.0 section and nothing else.
- Settings read back after the change:
  - ruleset active on `~DEFAULT_BRANCH` with its three rules;
  - `allow_merge_commit: false`;
  - private vulnerability reporting, Dependabot alerts and Dependabot security updates enabled;
  - CodeQL default setup configured, with its first run queued.

**Disclosed.**
- `pr-labels.yml` and `scorecard.yml` take effect only once they are on `main`. `pull_request_target`
  and `push: main` both run the workflow from `main`, so neither runs on its own PR. The Scorecard
  badge shows no score until the first run on `main`.
- The area table leaves gaps:
  - `nfs/aanf`, `nfs/sepp`, `nfs/tsctsf`, `libs/aka-crypto`, `libs/nas-alg-provider` and
    `libs/sms-core` are in no area.
  - `libs/analytics-features`, which the table lists, does not exist.
  - Assigning these is the maintainer's decision. Until then they get no area label.
- Dependabot and CodeQL PR runs that touch C++ or `.github/` also start the full CI on the
  self-hosted runner.
