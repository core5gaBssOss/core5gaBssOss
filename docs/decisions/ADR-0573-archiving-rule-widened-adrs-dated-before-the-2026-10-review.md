## ADR-0573: Archiving rule widened: ADRs dated before the 2026-10 review close unless they hold an open question; the live set is the pending work

**Date:** 2026-11-10. **Status:** Accepted (user, 2026-11-10: "I have TOO MANY ADRs now, please
clean the ADRs already DONE so that we can concentrate on pending ones"). Amends ADR-0465.

**Context.** After ADR-0465 (2026-10-06) `docs/decisions/` still held 453 live ADRs and
`archive/` 73. ADR-0465 closed an ADR only when no open-work marker word appeared anywhere in its
text (disclosed gap, deferred, stub, simplif, partial, ...). On this project nearly every ADR has a
"Disclosed" section, so the rule kept 322 finished ADRs live and the directory stopped telling
finished from pending. Since then the open items themselves were consolidated elsewhere:
REVIEW-5ae0ef1 (2026-10-24) swept the code base, `docs/COMPONENT_STATUS.md` records every
unbuilt component as "Already in Pipeline", `docs/COMPLIANCE_P1_P15.md` the hardening matrix, and
ADR-0572 the release order. An old ADR's "remains open" sentence is therefore no longer where open
work is tracked.

**Decision:** an ADR is Closed and moved to `docs/decisions/archive/` when a commit on
`origin/main` cites it (or, for the 36 ADRs older than the citing convention, the commit that
recorded them), and:

- dated before 2026-10-01: always, unless its text asks an *open question* for the user, or its
  status says *temporary* (ADR-0006) or *amended* (ADR-0422). Its disclosed limitations are
  tracked by the ledgers above, not by keeping it live.
- dated 2026-10-01 or later: only when its status carries none of *in progress, open, blocked,
  not yet, deferred, partial*, its text none of *not started, in progress, not yet
  implemented/built/wired/done, TODO, blocked, still/remains/left open*, and it is not a
  *Proposed* ADR that no code commit cites. ADR-0556..0562 and ADR-0572 (pipeline plans) stay
  live whatever their wording.

Applied with `scripts/docs/close_adrs.py` (unchanged): 402 ADRs closed on 2026-11-10, 52 live.
Links to moved files in `README.md` and `CHANGELOG.md` were rewritten to `decisions/archive/`
(an update in place, guardrail 10). `check_adr_index.py` passes. The 52 live ADRs are the
pending work, in four groups:

- *Open question for the user, never answered* (13): 0053, 0058, 0256, 0285, 0401, 0445, 0447,
  0448, 0471, 0488, 0500, 0504, 0524.
- *Work in progress or wired in part* (17): 0446, 0462, 0466, 0473, 0475, 0476, 0479, 0480,
  0482, 0483, 0486, 0487, 0534, 0536, 0551, 0553, 0563.
- *R19 pipeline and adoption plans* (17): 0539..0547, 0550, 0556..0562.
- *Process* (5): 0006 (temporary), 0422 (amended), 0465 (its own open items), 0572 (release
  plan), 0573 (this).

Where an ADR in the first two groups was in fact finished by a later ADR, the next person to
touch it adds a one-line status note and closes it; this ADR does not read all 52.

**Rejected alternatives.**

- **Keeping ADR-0465's marker rule and reading 322 ADRs one by one.** A week of review to
  reach a list the ledgers already hold.
- **Closing every ADR with a citing commit, dates and wording ignored.** It would have closed
  ADR-0558 (TMF640 in progress), the ADR-0539..0547 pipeline plans and the twelve ADRs that still
  hold a question the user never answered.
- **Deleting closed ADRs or their index rows.** ADR-0465 (c): the index would point nowhere.
- **A new status word ("Done") instead of Closed.** `close_adrs.py`, `check_adr_index.py` and
  ADR-0465 already define Closed.

**Disclosed.**

- The rule is a date and keyword heuristic plus one reading of the 60 candidates it kept, not an
  audit of the 402 it closed. A closed ADR whose work is incomplete is still closed; its open item
  must be in COMPONENT_STATUS, the REVIEW-5ae0ef1 ledger, COMPLIANCE_P1_P15 or ADR-0572 to be
  tracked. The 2026-11-10 evaluation (ADR-0572) found three ledger rows lagging the code, so that
  tracking is itself a v0.2.0 item.
- Nine ADRs with stale "not yet pushed" wording (0468, 0469, 0481: CI workflow changes that have
  run since; 0515, 0526, 0527, 0529, 0530, 0531: the 2026-10-24 review batch whose code commit did
  not cite them) were closed by inspection of their commits, not by the rule.
