# MOVE_LOG -- CLAUDE.md old section -> new location

All moves are verbatim; `python3 scripts/docs/verify_claude_md_move.py` checks that every sentence of the old CLAUDE.md (`git show main:CLAUDE.md`) is present in the new CLAUDE.md or `docs/project-context/`.

| Old section | New location | Action |
|---|---|---|
| Title + intro (6 lines) | CLAUDE.md | kept |
| Project goal, first paragraph | CLAUDE.md | kept |
| Project goal, 'The charging half...' paragraph | docs/project-context/charging-title-rationale.md | moved |
| Source of truth; Non-negotiable engineering rules; Working style; Guardrails | CLAUDE.md | kept |
| Mandated tech stack: Build/tooling, HTTP/2, Testing, Deployment bullets | CLAUDE.md | kept |
| Mandated tech stack: Codegen, JSON, Async, PFCP, Storage, Observability, GUI, AI/ML bullets | docs/project-context/tech-stack-details.md | moved |
| Scope: Tier 1, Reference points | CLAUDE.md | kept |
| Scope: Tier 2, Tier 3 | docs/project-context/scope-tier2-tier3.md | moved |
| Charging domain: CHF bullet, SID bullet (list), Deliverable bullet | CLAUDE.md | kept |
| Charging domain: SID bullet parenthetical (TM Forum API extension history) | docs/project-context/tmforum-extension-history.md | moved |
| AI pipelines (NWDAF-centric) | docs/project-context/ai-pipelines.md | moved |
| Definition of done: item 7 first sentences (config:ro rule) | CLAUDE.md (single copy; definition-of-done.md points to it instead of repeating it) | kept |
| Definition of done: all nine items, full text | docs/project-context/definition-of-done.md (skill new-nf-checklist points here) | moved |
| Phase 9 -- production documentation | docs/project-context/phase9-production-documentation.md | moved |
| Project decisions (resolved at kickoff) | docs/project-context/kickoff-decisions.md | moved |
| Reality check | docs/project-context/reality-check.md | moved |
| Commercialization mandate (ADR-0049) | docs/project-context/commercialization-mandate.md | moved |
| Charging data-plane consistency/performance (ADR-0445) | docs/project-context/adr-0445-charging-dataplane-debt.md | moved |
| NEW (not from the old file): Context discipline, Compact instructions, Session start, Moved-context pointers | CLAUDE.md | added |
| Project goal: the R19 / 6G name rationale (2026-10-29 trim; whole original paragraph kept there) | docs/project-context/project-name-rationale.md | moved; CLAUDE.md keeps the goal + pointer |
| Guardrail 9: incident history and enforcement-layer detail (2026-10-29 trim) | docs/project-context/guardrail-history.md | moved; CLAUDE.md keeps the rule, the override clause, the hook step + pointer |
| Guardrail 10: incident history and enforcement detail (2026-10-29 trim) | docs/project-context/guardrail-history.md | moved; CLAUDE.md keeps every rule sentence + pointer |
| Definition of done item 7: the ADR-0453 incident parenthetical (2026-10-29 trim) | docs/project-context/guardrail-history.md | moved; CLAUDE.md keeps the rule |
| Session start: the duplicate attribution reminder (2026-10-29 trim; guardrail 9 already says it) | docs/project-context/guardrail-history.md | moved |

Note: `docs/DECISIONS.md` was separately split (scripts/docs/split_decisions.py); CLAUDE.md's references to it stay valid because the index keeps the path.

Added 2026-10-06 at the user's direction: Guardrails 6-8 in CLAUDE.md are PROMPT.md standing rules 5-7 (verbatim text; guardrails 1-5 were only re-wrapped onto single lines, no wording change). The README "Capability-completeness gap-closure" section was moved verbatim to `docs/CAPABILITY_GAP_ANALYSIS.md` (relative links adjusted) and README keeps a summary + link.
