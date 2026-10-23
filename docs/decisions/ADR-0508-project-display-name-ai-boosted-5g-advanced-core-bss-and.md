## ADR-0508: Project display name AI-Boosted 5G-Advanced Core, BSS and OSS; README cut to a project page, detail moved to docs/OVERVIEW.md

**Date:** 2026-10-22. **Status:** Accepted. The README cut (and the move to `docs/OVERVIEW.md`) is reversed by
ADR-0511 (user-directed, 2026-10-23); the display name and the rest of this ADR stand.

**Context.** Phase 2 of the professional-setup review (2026-10-22; Phase 1 is ADR-0507) covers how
the project presents itself. Three things were off:
- **The name.** The README title image read "AI-Native 5G-Advanced Core, Charging & BSS — R19. Built
  Forward to 6G". CLAUDE.md leaves 6G out of the title on purpose, because no 6G stage-3
  specification exists. The repository description and the architecture diagram used a third
  variant, "AI-Boosted 5G-Advanced Core and Charging".
- **The README.** It ran to 423 lines. The full status table, the products the charging model
  supports, the open-source product list, the AI capability table and the 33-series security table
  all sat on the front page. A reader could not find what the project is, its status, and how to
  build it.
- **Root clutter.** `openapitools.json` configured openapi-generator-cli. ADR-0010 chose the
  project's own Jinja2 generator in its place, and nothing has read the file since Phase 0.

**Decision:**
- **Display name: "AI-Boosted 5G-Advanced Core, BSS and OSS"** (user-decided). It is used in the
  README title image, the architecture diagram title, `NOTICE` and the repository description.
  "Charging" is covered by BSS. The planned GitHub organization handle is `core5gaBssOss`. Creating
  the organization and moving the repository are Phase 4, so the repository slug, `REUSE.toml`'s
  package name and the build identifiers (`vcpkg.json`, the CMake project) are unchanged.
- **README cut to 126 lines.** It now holds badges, the title, what the project is, a one-line
  status per area, getting started, a documentation index, contributing and the license. The README
  still says plainly that the project is not deployable in production yet, and links to the
  blocker list.
- **`docs/OVERVIEW.md` gets the rest, moved verbatim** with relative links adjusted. One line
  changed: the repository layout's `specs/` entry still described specification PDFs, which
  ADR-0507 removed.
- **Title image regenerated** with `scripts/docs/render_title_svg.py`. Its second line now reads
  "3GPP Release 19".
- **`.gitattributes` added:**
  - LF line endings for shell scripts.
  - `specs/` kept byte-for-byte as vendored.
  - Vendored and generated files left out of GitHub's language statistics.
- **`openapitools.json` deleted.**

**Rejected alternatives.**
- *"AI based 5G+ BSS and OSS"*, the user's first proposal:
  - It drops the core network, which is most of the code. On 2026-10-22, `nfs/` held about 100,000
    lines across 30 NFs; `bss/` held about 7,000.
  - "5G+" is a marketing term, not 3GPP's.
  - "AI based" claims more than the "model informs, engine decides" rule allows.
- *`open5ga` as the handle.* It is one letter from Open5GS, an established project.
- *Moving `PROMPT.md` and `CHARGING_PROMPT.md` into `docs/project-context/`.* 69 files cite them,
  including code comments in `bss/` and accepted ADRs. Moving them now would churn those files and
  conflict with open PRs. Deferred, not dropped.
- *Moving `overlay-ports/` and `snow3g/` under `third_party/`.* `overlay-ports/` is vcpkg's own
  convention and is referenced from `vcpkg-configuration.json`. `snow3g/` holds no third-party code
  (ADR-0507's review): only a framework design for the operator's licensed package, and the
  fingerprint list a CI guard reads.
- *Coverage, docs and OpenSSF Scorecard badges now.* No coverage report, docs site or Scorecard
  workflow exists, and a badge must measure something real. They come with the Phase 3 CI work.
- *markdownlint and yamllint in CI now.* The existing docs would fail markdownlint wholesale, so
  the rules need choosing first. Deferred to Phase 3, which changes CI anyway.
- *`.git-blame-ignore-revs`.* The history has no bulk reformatting commit, only small per-file
  `clang-format` fixes, so there is nothing worth hiding from blame.

**Disclosed.**
- `docs/OVERVIEW.md` is moved, not refreshed. Some rows were already stale in the README and are
  still stale there. For example, the 33.501 row says "no SEPP/N32", and the AKMA row says "Not
  built yet", while the Phase 6 status row lists the SEPP and AAnF as built in part.
- `vcpkg.json`'s description and the CMake `DESCRIPTION` still carry the old name. Editing
  `vcpkg.json` would invalidate the CI vcpkg cache, whose key hashes that file. Both are technical
  identifiers.
