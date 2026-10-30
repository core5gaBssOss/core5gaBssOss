## ADR-0512: README.md is append/update only: nothing is ever removed from it (user-directed, strict); pre-commit hook and CI check

**Date:** 2026-10-23. **Status:** Accepted (user-directed, strict).

**Context.** ADR-0508 cut `README.md` from 417 lines to 128 on 2026-10-22. The user found it the
next day, and ADR-0511 restored the full README. The user then gave a standing order, in their
words: "I WANT THE README to APPENDED/UPDATED as and when project MOVES ON - NEVER /EVER REMOVE
ANYTHING FROM README - THIS IS VERY VERY STRICT COMMAND".

**Decision:**
- `README.md` is append/update only. As the project moves on, content is added to it and lines are
  updated in place: a status that moves on, a corrected row, a new NF, product or section. No
  section, row, paragraph or detail is ever deleted, shortened, or moved out to another document. A
  heading rename counts as a removal. A change that would remove content needs the user's explicit
  approval, asked before the edit is made.
- The rule is CLAUDE.md guardrail 10, so every session reads it. It is also in the definition of
  done (item 9: the NF's README rows are appended or updated) and in `.github/CONTRIBUTING.md`.
- Enforcement, following guardrail 9's layered pattern:
  - `scripts/docs/check_readme_append_only.py BASE [HEAD]` fails when a section heading
    (`<h2>` or Markdown) disappears, or when the non-blank line count or the word count goes down.
    It lists every base line no longer present verbatim, so a reviewer sees each in-place update.
  - `.githooks/pre-commit` runs it on a commit that stages `README.md`, comparing `HEAD` with the
    index, and rejects the commit. It is activated by the same `git config core.hooksPath .githooks`
    step as the `commit-msg` hook.
  - The `readme-append-only` job in `.github/workflows/docs-lint.yml` runs it on every PR (against
    the PR base) and every push to `main` (against the previous head), on a GitHub-hosted runner.
- Checked against the README's real history: the ADR-0508 cut fails (12 headings removed,
  354 to 103 lines, 6417 to 723 words); #32's small in-place edit passes. A staged README with one
  section deleted is rejected by the hook (exit 1).

**Rejected alternatives.**
- *Every base line must survive verbatim.* That forbids updating a line in place, which the user
  asked for ("appended/updated").
- *Make the CI job a required status check.* `main` has no required checks by design (ADR-0509: one
  self-hosted runner, about 40 minutes per build). The job is a loud detection layer, like
  `attribution-check`; the pre-commit hook is the local gate.
- *Memory or CLAUDE.md alone.* The ADR-0508 cut happened with CLAUDE.md in force. A rule that only
  a reader enforces did not hold, so the check runs in git and CI as well.

**Disclosed.**
- The check measures size and headings, not meaning. A change that keeps the line and word counts
  while replacing content with different text passes; review must still catch that.
- Under this check the ADR-0511 restore itself would have been flagged: the short README's "Getting
  started" and "Contributing" headings became "Building" and "Contributing / working style". From
  now on the full README on `main` is the baseline.
- A merge made in the GitHub web UI is not seen by the local hook. The CI job sees it on the push
  to `main`, after the fact.
- The hook works only in a clone that has run `git config core.hooksPath .githooks`.
