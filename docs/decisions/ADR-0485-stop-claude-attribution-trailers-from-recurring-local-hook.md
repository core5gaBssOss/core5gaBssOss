## ADR-0485: Stop Claude attribution trailers from recurring: local hook, settings.json, CI detection job

**Date:** 2026-10-13. **Status:** Accepted, built and verified (hook tested locally, CI job pushed).

**Context.** User-directed, mandatory, repeated incident: 76 commits across this repo's history
(2026-09-05 to 2026-10-13, across `main` and several branches) carried a `Co-Authored-By:
Claude...` / `Claude-Session:` trailer, driven by a per-session default (the system prompt's own
built-in git-commit instruction) that keeps asserting itself regardless of this project's own
intent. A `backup-before-trailer-strip` branch proves a prior session already did ONE git-history
cleanup (around 2026-09-10); 29 more trailer-carrying commits accumulated in the month after it,
because only history was cleaned, not the recurring cause. A research pass (claude-code-guide
agent) plus a second-opinion review (Fable advisor) found: filed upstream issues (claudeissues
#53259, #65657, #11101) report the built-in attribution instruction overriding both
`.claude/settings.json` and CLAUDE.md on their own; `main` has no branch protection at all (no
required status checks, no required reviews), confirmed via `gh api .../branches/main/protection`
-> 404; and the exact failure mode has real precedent in this repo already, at commit `835081c` (a
squash-merge that reintroduced trailers from the original PR branch's individual commits -- a path
no local git hook can ever reach, since GitHub builds a squash-merge message server-side).

**Decision:** four layers, since no single one is trusted alone (disclosed per-layer limits
below) -- `.githooks/commit-msg` (tracked, `git config core.hooksPath .githooks` activates it per
clone) REJECTS, not silently strips, a commit message matching
`Co-[Aa]uthored-[Bb]y:.*([Cc]laude|[Aa]nthropic)|Claude-Session:|Generated with Claude Code` --
tested locally both ways (rejects the bad case, passes the clean case). `.claude/settings.json`
gets `"attribution": {"commit": "", "pr": "", "sessionUrl": false}` and `"includeCoAuthoredBy":
false` -- the EXACT shape (empty strings, not booleans) was confirmed against Claude Code's own
live JSON-schema validator after an initial guess (boolean `attribution.commit`/`attribution.pr`,
and a `commits`/`pullRequests` boolean pair from a third-party guide) was rejected by it; the
validator's error output is treated as ground truth over any external source, since it IS the
actual schema this Claude Code install enforces. CLAUDE.md gets guardrail 9 (repeated in both the
Guardrails section and Session start, per the advisor's point that Guardrails is a mid-drift
recovery mechanism read after the fact, while Session start is read before any commit is
attempted, even in a compacted/abbreviated-context session). `.github/workflows/ci.yml` gets a new
`attribution-check` job (hosted runner, both `push` and `pull_request`, independent of whether
`changes` found code or docs-only) that scans every commit in the pushed/PR range plus the PR
body for the same pattern and fails the run -- the only layer that can see a GitHub-web-UI-built
commit message (squash/rebase merge, or an API-created commit) at all, closing the exact gap
`835081c` is live precedent for.

**Disclosed limits, by layer.** `.githooks/commit-msg`: bypassed by `--no-verify` (already
forbidden generally by this session's own standing instructions, reinforced here); never runs for
a commit GitHub's web UI constructs server-side (squash-merge, rebase-merge, an API call) --
`attribution-check` is the only layer reaching that path, and only after the fact. `.claude/settings.json`:
best-effort per the filed issues above -- may be overridden by the same built-in instruction that
caused this incident; not relied on alone. `attribution-check`: detects, does not block. `main` has
no required-status-check branch protection (confirmed via the GitHub API, not assumed) by design --
every commit in this project's history, including every one from this session, has gone directly
to `main` with no PR, so requiring a passing status check would mean requiring a PR for every
change, a materially different workflow this ADR does not impose unasked. If the user wants that
enforced (not just detected), branch protection requiring `attribution-check` is a separate,
explicit decision to make, not bundled into this fix.

**Not attempted:** disabling `includeGitInstructions` (a real, documented Claude Code setting that
turns off the built-in git/PR workflow instructions in the system prompt entirely, which plausibly
includes the instruction that keeps re-asserting attribution) -- a broader change than this
incident's own scope, since it removes more than just the attribution line, and its side effects
cannot be fully verified from inside a single session. Flagged in CLAUDE.md as the next lever if
the four layers above still do not hold.

**Rejected alternatives.** Silently auto-stripping the trailer in the hook instead of rejecting:
would hide that the upstream default is still trying to inject it, the actual thing worth keeping
visible until it stops recurring (advisor's own point). Relying on CLAUDE.md prose alone: already
disproven by this exact incident's own history -- it was never going to be enough on its own.
Branch protection requiring the new CI check: changes this project's existing direct-push-to-`main`
workflow for every future change, not just this one; left as an explicit opt-in the user can
request separately, not assumed.
