## ADR-0511: README restored to the full project page, updated; reverses ADR-0508's README cut (user-directed)

**Date:** 2026-10-23. **Status:** Accepted (user-directed).

**Context.** ADR-0508 cut `README.md` from 417 lines to 128 and moved the rest to
`docs/OVERVIEW.md`. On 2026-10-23 the user asked for the full README back on `main`: the README is
the project's front page and must carry all the project's details, updated, never reduced.

**Decision:**
- `README.md` is the full README as it stood before ADR-0508 (commit `f484606`), with every section
  kept: architecture, source of truth, the full phase status, open-source and commercial products,
  what that means in practice, AI capabilities, 33-series security compliance, capability gap
  closure, repository layout, building, contributing, working with the project, and the license.
- The updates `main` gained since are applied to it:
  - badges with the `core5gaBssOss/core5gaBssOss` URLs and the OpenSSF Scorecard badge (ADR-0509,
    ADR-0510), and every repository link moved to the new organization;
  - the regenerated title image's alt text, with the user-decided display name (ADR-0508);
  - the short README's at-a-glance list and its "not deployable in production yet" statement, and
    its one-line-per-area status table ahead of the phase table;
  - the repository layout's `specs/` entry, which described specification documents ADR-0507
    removed;
  - the lab command, a documentation index (with `CHANGELOG.md`), and the third-party licensing
    notice (ADR-0507);
  - the contributing paragraph: `CODEOWNERS`, labels with the one-area check and the `main`
    ruleset are done (ADR-0509), the organization move is done (ADR-0510); the CLA and
    `bootstrap.sh` are still not done.
- Four security-compliance rows that ADR-0508 disclosed as already stale are corrected from their
  ADRs, each with the verification level `docs/SECURITY_COMPLIANCE.md` records: 33.501 (NRF
  discovery authorization, ADR-0477; SEPP in part, ADR-0491-0494), 33.210 (opt-in TLS 1.2 floor,
  ADR-0478, unit-tested only), 33.117 (duplicate JSON keys rejected, ADR-0476, server hook
  compile-checked only) and 33.535 (AAnF in part, ADR-0495-0497).
- Five markdownlint findings in the old text (two lists without a blank line before them, two
  doubled blank lines, one code fence without a language) are fixed, so the README passes the
  ADR-0509 docs-lint job. No wording changed for them.

**Rejected alternatives.**
- *Keep the short README and link `docs/OVERVIEW.md` more prominently.* The user rejected the cut.
- *Revert ADR-0508's commit.* It also carried the display name, the title image, `.gitattributes`
  and the `openapitools.json` removal, which stay; and the later README edits (ADR-0509, ADR-0510)
  would have been lost.

**Disclosed.**
- `docs/OVERVIEW.md` is left as ADR-0508 created it. It now duplicates most of the README and its
  preamble ("moved ... so the README can stay short") is no longer true; removing it, or cutting it
  back to a pointer, is left for the user to decide.
- `docs/SECURITY_COMPLIANCE.md`'s summary table still says F3/F4/F8 open and AKMA not
  implemented; only its per-finding status notes are current. The README rows follow those notes.
- Other README rows were not re-verified against the code; the phase status table is the
  pre-ADR-0508 text apart from the rows named above.
