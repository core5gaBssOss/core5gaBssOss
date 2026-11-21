## ADR-0591: SMSF 404s carry CONTEXT_NOT_FOUND (SBI-03)

**Date:** 2026-11-19. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), SMSF part. The
SMSF had 3 cause-less 404s, all for a SUPI with no UE SMS context. `send-mt-sms` already carried
`CONTEXT_NOT_FOUND`. TS 29.540 (j60) is in `specs/3gpp/MANIFEST.tsv`.

**Decision:**

- The 404s of SMSServiceParameterUpdate (PATCH `ue-contexts/{supi}`), SMServiceDeactivation
  (DELETE `ue-contexts/{supi}`) and UplinkSMS (POST `ue-contexts/{supi}/sendsms`) carry
  `CONTEXT_NOT_FOUND`. TS 29.540 tables 6.1.3.3.3.3-3, 6.1.3.3.3.2-3 and 6.1.3.3.4.2.2-2: "the UE
  context for SMS to be operated is invalid or not found in SMSF". For PATCH the table also lists
  `USER_NOT_FOUND`; the SMSF answers this 404 only when it holds no context, so
  `CONTEXT_NOT_FOUND` is the one that applies.
- Test: `AmfSmsRegistration.NoSmsRequestedMeansNotAllowedAndNoSmsfContext` already expected the
  PATCH 404 and now also asserts the cause.

Disclosed:

- The DELETE and sendsms causes have no test of their own; they share the PATCH's code path shape
  (the same `contexts` lookup and the same `problem_response` call).
- Compile-checked locally only (this host is a CI runner); CI on PR #41 passed: build, lint, asan-ubsan and tsan (2026-11-19).

**Rejected alternatives.**
- `USER_NOT_FOUND` on the PATCH 404: TS 29.540 uses it for a subscriber unknown to the UDM, which
  this SMSF does not check on PATCH.
