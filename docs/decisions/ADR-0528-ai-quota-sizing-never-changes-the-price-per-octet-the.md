## ADR-0528: AI quota sizing never changes the price per octet: the reservation follows the grant (CHG-01, amends ADR-0074/0297)

**Date:** 2026-10-24. **Status:** Accepted (user decision 2026-10-24).

**Context.** REVIEW-5ae0ef1 CHG-01: the AI quota model scales a volume grant by
`m = clamp(predicted/base, 0.5, 2.0)`, but the money reserved stayed the ProductOfferingPrice amount
`P`; ADR-0297's proportional debit (`reserved x used/granted`) then charged `P/(m*B)` per octet, so
the model changed the price within a 4x band -- against guardrail 7 ("the model informs, the
deterministic engine decides"). ADR-0074 asserted compliance before ADR-0297 created the
interaction. Asked whether an enlarged grant should also enlarge the reservation, the user answered
"Both", and that it must be configurable in the GUI.

**Decision.**
- `chf::ai_scaled_grant` (`nfs/chf/src/proportional_debit.cpp`): the grant becomes `B*m` (whole
  octets) and the reservation `P * (B*m)/B`, using the multiplier actually applied after rounding. So
  `reserved/granted = P/B` and the debit for any usage is what it would be with the AI off. The model
  decides how much is granted per round; the price stays the catalog's.
- The multiplier bounds, fixed at 0.5 / 2.0 before, are operator configuration:
  `config/chf.json` `ai_quota_multiplier_min` / `_max` (env `CHF_AI_QUOTA_MULTIPLIER_MIN` / `_MAX`),
  validated `0 < min <= 1 <= max` at startup. With `ai_quota_sizing_enabled` they appear in the
  operator GUI's CHF configuration form (schema derived by `gui/schema-gen`, ADR-0425), which writes
  the file; the change applies at the CHF's next restart.
- ADR-0074 (compliance claim) and ADR-0297 (proportional debit) are amended by this ADR: their
  combination is compliant only with the reservation following the grant.

**Rejected alternatives.**
- *Keeping the reservation at `P` and dividing the debit by the base grant `B`* (the review's fix
  hint): usage between `B` and `B*m` would be capped at `P`, i.e. given away -- a price change in
  the other direction.
- *A mode switch between "volume only" and "volume and reservation".* "Volume only" cannot keep the
  price per octet with proportional debit (above), so it would reintroduce the violation.

**Disclosed.**
- The bounds are applied at CHF startup only (as every CHF setting); no live reload.
- A larger reservation needs the subscriber's balance to cover it; a reservation the balance cannot
  cover fails as any reservation does (no fallback to the base grant).
- Money stays `double` here, as in the rest of the engine (`bss_sid::Money`).

**Verified (2026-10-24).** See below.
