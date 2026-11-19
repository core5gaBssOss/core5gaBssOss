## ADR-0588: AMF 404s carry CONTEXT_NOT_FOUND; CancelLocation for an unknown session is 403 LOCATION_SESSION_UNKNOWN (SBI-03, AMF part)

**Date:** 2026-11-19. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), AMF part. The
AMF answered 15 errors for an unknown resource with a 404 and no `cause`. TS 29.518 lists the
404 cause per operation.

**Decision:**

- 10 404s for an unknown UE context now carry `CONTEXT_NOT_FOUND`. These are the Namf_Communication
  `ue-contexts/{ueContextId}` custom operations (table 6.1.7.3-1), Namf_Location
  `provide-loc-info` (table 6.4.7.3-1) and the Namf_MT `ue-contexts/{ueContextId}` GET and
  `ue-reachind` PUT (table 6.3.7.3-1).
- `cancel-pos-info` for an unknown `ldrReference` changes from 404 to **403
  `LOCATION_SESSION_UNKNOWN`**. Table 6.4.3.2.4.4.2-2 lists no 404 for this operation, and table
  6.4.7.3-1 gives an unknown location session this cause. This is a status-code change, not only
  a cause. Test `AmfLocationIntegration.CancelLocationIs404` is renamed
  `CancelLocationUnknownSessionIs403`.
- Tests: the AMF integration tests that already expected a 404 for an unknown UE context now also
  assert `CONTEXT_NOT_FOUND` (release, release again, relocate, provide-loc-info, MT domain
  selection, MT UE reachability).

Disclosed: four 404s stay without a cause.

- Two Namf_MBSBroadcast 404s for an unknown MBS context: table 6.5.7.3-1 defines no application
  errors.
- Two N1N2MessageTransfer 404s for a UE context that exists but has no live NGAP association.
  `CONTEXT_NOT_FOUND` would be false, because the context exists. The spec answer for a CM-IDLE
  UE is paging, which is deferred for future implementation (ADR-0523).

Compile-checked locally only (this host is a CI runner). CI on PR #37 passed: build, lint, asan-ubsan and tsan (2026-11-19).

**Rejected alternatives.**
- Keep 404 on `cancel-pos-info` and add a cause: TS 29.518 defines no 404 cause for it, so any
  cause would be invented.
- `CONTEXT_NOT_FOUND` on the N1N2 "no NGAP association" 404: the UE context is present, so the
  cause would be false.
