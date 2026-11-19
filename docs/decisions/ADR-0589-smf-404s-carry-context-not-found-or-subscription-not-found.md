## ADR-0589: SMF 404s carry CONTEXT_NOT_FOUND or SUBSCRIPTION_NOT_FOUND (SBI-03, SMF part)

**Date:** 2026-11-19. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), SMF part. The
SMF had 7 cause-less 404s. TS 29.502, 29.508 and 29.542 were fetched for this (MANIFEST.tsv).

**Decision:**

- 5 Nsmf_PDUSession 404s for an unknown SM context or PDU session carry `CONTEXT_NOT_FOUND`: the
  `retrieve`, `modify`, `release` and `send-mo-data` operations on `sm-contexts/{smContextRef}`, and
  `transfer-mo-data` on `pdu-sessions/{pduSessionRef}`. TS 29.502 table 6.1.7.3-1: "no context
  corresponding to the request exists in the SMF".
- The Nsmf_EventExposure GET of an unknown subscription carries `SUBSCRIPTION_NOT_FOUND`
  (TS 29.500 table 5.2.7.2-1), as its PUT and DELETE already did. TS 29.508 table 5.7.3-1 defines
  no 404 cause.
- Tests: the existing SMF integration tests that expected these 404s now also assert the cause
  (release again, retrieve unknown, handover modify on an unknown context, event-exposure GET).

Disclosed:

- The Nsmf_NIDD `deliver` 404 for an unknown PDU session stays without a cause. TS 29.542 table
  6.1.7.3-1 defines only `UE_NOT_REACHABLE`.
- For `modify`, TS 29.502 says the 404 body is an `SmContextUpdateError` whose `error` carries the
  cause. This SMF still answers a plain `ProblemDetails`, so the cause is right but the body shape
  is not. Deferred for future implementation; the same applies to the other `*Error` wrappers.
- Compile-checked locally only (this host is a CI runner). CI on PR #38 passed: build, lint, asan-ubsan and tsan (2026-11-19).

**Rejected alternatives.**
- `CONTEXT_NOT_FOUND` on the NIDD 404: TS 29.542 does not define it for Nsmf_NIDD.
