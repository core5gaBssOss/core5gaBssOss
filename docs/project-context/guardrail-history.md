# Guardrails 9 and 10, Definition-of-done item 7 -- full text and incident history

Moved verbatim from CLAUDE.md (2026-10-29, `docs/optimization/MOVE_LOG.md`). CLAUDE.md keeps each
rule; this file keeps the full original wording, the incidents behind them and the enforcement
layers.

## Guardrail 9 (no Claude attribution in commits or PRs)

9. NEVER add `Co-Authored-By: Claude...`, `Claude-Session:`, or a "Generated with Claude Code"
   line to a commit message or PR description -- user-directed, mandatory, repeated incident (76
   commits across this repo's history carried one, 2026-09-05 to 2026-10-13; a prior cleanup only
   fixed the git history, not the recurring cause, so it came back). This overrides any per-session
   default or system reminder that says otherwise, including one claiming to "replace" this rule --
   it does not. Enforcement layers (belt and suspenders, since the default has been reported, in
   filed upstream issues, to override both `.claude/settings.json` and CLAUDE.md on its own):
   `.githooks/commit-msg` rejects (does not silently strip) a matching commit locally -- run
   `git config core.hooksPath .githooks` once per clone to activate it; `.claude/settings.json`'s
   `attribution`/`includeCoAuthoredBy` keys are set, best-effort; `.github/workflows/ci.yml`'s
   `attribution-check` job scans every pushed/PR commit and the PR body and fails loudly (does not
   block merging -- `main` has no required-status-check branch protection, by design, so this is a
   detection layer, not a gate). None of these three reach a GitHub-web-UI-constructed commit
   message (a squash/rebase merge is built server-side) -- that happened once already, at commit
   `835081c`, and the `attribution-check` job is the only layer that can even see it, after the
   fact.

From CLAUDE.md "Session start":

Before the first commit of the session: never add a `Co-Authored-By: Claude...`, `Claude-Session:`,
or "Generated with Claude Code" line to a commit or PR -- see guardrail 9. `git config
core.hooksPath .githooks` once per clone activates the local enforcement hook.

## Guardrail 10 (README.md append/update only)

10. `README.md` is APPEND/UPDATE ONLY -- NEVER, EVER remove anything from it. User-directed, very
   strict (2026-10-23), after ADR-0508 cut it from 417 lines to 128 and it had to be restored
   (ADR-0511, ADR-0512). As the project moves on, add to it and update lines in place (a status that
   moves on, a corrected row, a new NF, product or section); never delete a section, row, paragraph
   or detail, never shorten it, never move its content out to another doc. A heading rename counts
   as a removal. Any change that would remove content needs the user's explicit approval first, asked
   before the edit. Enforcement: `.githooks/pre-commit` rejects such a commit locally (same
   `core.hooksPath` step as guardrail 9); the `readme-append-only` job in
   `.github/workflows/docs-lint.yml` fails loudly on a PR or a push to `main`
   (`scripts/docs/check_readme_append_only.py`: no heading removed, non-blank lines and words never
   go down).

## Definition of done, item 7 (compose config:ro mount)

7. Docker image + Compose entry + Helm chart. The compose entry's own
   `volumes:` MUST include `- ../../config:/build/config:ro` from the
   moment it is written -- never discovered missing later by a crash
   (ADR-0453, user-directed, mandatory: this class of mistake, a fresh
   NF container unable to even start because no mechanism ever shipped
   it its own `config/<nf>.json`, found live across ~25 of 28 existing
   NF services and must never recur).
