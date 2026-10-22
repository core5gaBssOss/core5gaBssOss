## ADR-0510: GitHub organization core5gaBssOss: repository moved to core5gaBssOss/core5gaBssOss, one repository plus an org profile, runner stays repository-level

**Date:** 2026-10-22. **Status:** Accepted. The transfer itself waits for the organization to be
created.

**Context.** Phase 4 of the professional-setup review (Phases 1-3: ADR-0507, ADR-0508, ADR-0509)
moves the project from a personal account to a GitHub organization, as OCUDU and other open 5G
projects are organized. ADR-0467 deferred the organization move until just before outside
contributors are invited. The user chose the handle `core5gaBssOss` and the display name
"AI-Boosted 5G-Advanced Core, BSS and OSS" (ADR-0508). Only the account owner can create an
organization: GitHub's API cannot.

**Decision (user-decided, 2026-10-22):**
- **Repository name `core5gaBssOss`:** `github.com/core5gaBssOss/core5gaBssOss`. This follows the
  convention for a project's main repository (open5gs/open5gs, free5gc/free5gc, ocudu/ocudu).
- **One repository, plus an organization profile.**
  - NFs, BSS, GUI, Helm charts and docs stay together.
  - A `core5gaBssOss/.github` repository holds `profile/README.md`, the organization's landing page.
- **The self-hosted runner stays registered to the repository.** If the transfer disconnects it, it
  is re-registered against the new URL, following `docs/project-context/host-toolchain.md` step 6.
- **In-repo references** to `prajithparan/AI-Boosted-5G-Advanced-Core-and-Charging-R19` now point
  to `core5gaBssOss/core5gaBssOss`:
  - the README badges and links, `CHANGELOG.md` and the issue-template links;
  - `SECURITY.md` and `CONTRIBUTING.md`;
  - `REUSE.toml` (package name and download location);
  - the runner step in `host-toolchain.md`.

  Commit maps under `docs/history/` and accepted ADRs are records and keep the old URL.
  `CODEOWNERS` keeps `@prajithparan`, the maintainer's own account.
- **Order:**
  1. The user creates the organization.
  2. The repository is transferred and renamed in one API call.
  3. The runner, CI, rulesets, code scanning, Dependabot and the redirects are checked.
  4. The profile repository is created.
  5. This change merges.

  Merging it before the transfer would point every badge and link at a repository that does not
  exist yet.

**Rejected alternatives.**
- *`5gc-r19` as the repository name.* It matches the CMake project and local folder, but puts R19
  in the URL after Release 20.
- *`core`.* Short, but undersells BSS and OSS.
- *Keeping the current name.* Long, and it says "Charging", not the display name.
- *Splitting out Helm charts.* It breaks the definition-of-done rule (item 7, ADR-0453) that an NF
  change ships its Compose entry and Helm chart together.
- *A separate docs-site repository.* It needs a site generator and an ongoing sync from `docs/`.
  GitHub Pages can publish from this repository later if a site is wanted.
- *Moving the runner to the organization now.* That needs sudo on the host and an `admin:org`
  token. Nothing else in the organization would use the runner yet.

**The old URL must keep redirecting (user-directed, 2026-10-22).**
- `https://github.com/prajithparan/AI-Boosted-5G-Advanced-Core-and-Charging-R19` has been shared
  and must always lead to the new repository. GitHub redirects it permanently, for both web and git
  URLs.
- One action breaks the redirect, and it cannot be restored afterwards: creating a repository, or a
  fork, named `AI-Boosted-5G-Advanced-Core-and-Charging-R19` under `prajithparan`. **Never create
  one.**
- The redirect is tested after the transfer: the web URL and `git ls-remote` against the old URL.

**Disclosed.**
- GitHub's transfer documentation (checked 2026-10-22) says webhooks, secrets, deploy keys, issues,
  PRs, stars and forks move with the repository. It says web and git URLs redirect, until a
  repository or fork is created at the old location. It does not say what happens to
  repository-level self-hosted runners, rulesets or code-scanning settings. Those are checked after
  the transfer, not assumed.
- The OpenSSF Scorecard result and badge are keyed by repository URL, so they start again under the
  new one.
- ADR-0467's runner group, which would keep fork PRs off the self-hosted runner, is still not built.
  The fork-PR approval setting is the barrier until then.
