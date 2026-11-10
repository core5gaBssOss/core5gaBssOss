## ADR-0548: README append-only check accepts user-approved heading renames

**Date:** 2026-11-01. **Status:** Closed (work pushed; last citing commit d05994be on origin/main, 2026-11-10). Accepted.

**Context.** CLAUDE.md guardrail 10 (ADR-0511, ADR-0512) makes README.md append/update only. A heading
rename counts as a removal and needs the user's explicit approval first. `scripts/docs/check_readme_append_only.py`
(pre-commit and CI `readme-append-only`) failed on every heading that disappeared, and it had no way
to tell an approved rename from a removal. On 2026-11-01 the user directed that nothing be called a
"gap", only "deferred for future implementation". They then approved renaming the README headings
explicitly: "Yes  , rename all Gaps as defiered for future implementation or something like that".
Two README headings carried the word:
- `Capability-completeness gap-closure` → `Capability-completeness closure of deferred items`
- `3GPP specification gaps` → `3GPP specification open points and deferred items`

**Decision:**
- Approved renames are recorded in `docs/project-context/readme-approved-heading-renames.txt`, one per
  line: `old => new | date | quoted approval`.
- The checker accepts a missing base heading only when that file lists it AND the new heading is
  present in the head. It prints each approved rename it accepts.
- A line without a quoted approval makes the checker exit with an error.
- Line-count and word-count checks are unchanged, so a rename that also cuts content still fails.
- An unlisted rename still fails (negative test run locally).
- The same change rewords "gap" in its status sense to "deferred item" across the docs, prompts and
  deploy/CI comments. The README link to the moved `CAPABILITY_GAP_ANALYSIS.md` section follows its new anchor.
- Deliberately unchanged:
  - technical uses of the word: CDR sequence gaps, SQN gap, ARP gap, Threshold/Quota gap, gap
    detection, and N-number gaps in the reference-point numbering
  - code and metric identifiers (`chf_cdr_sequence_gap_total`)
  - file names (`CAPABILITY_GAP_ANALYSIS.md`)
  - ADR file names and titles
  - historic records (`docs/reviews/REVIEW-5ae0ef1.md`, `docs/optimization/*`)

**Rejected alternatives.**
- *Accept a one-off CI failure on the rename PR*: this leaves the main-branch check red or needs an
  admin merge, and the next PR would hit the same failure.
- *An override flag or environment variable on the checker*: anyone could set it without the user's
  approval being recorded anywhere, which guts guardrail 10.
- *Keep the old heading text and reword only the body*: this goes against the user's direct
  instruction to rename the headings.
