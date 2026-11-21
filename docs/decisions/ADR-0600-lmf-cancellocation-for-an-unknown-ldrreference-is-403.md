## ADR-0600: LMF CancelLocation for an unknown ldrReference is 403 LOCATION_SESSION_UNKNOWN (SBI-03, LMF part)

**Date:** 2026-11-20. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), LMF part. The LMF
had one cause-less 404: CancelLocation (`POST {apiRoot}/cancel-location`, Nlmf_Location) for an
`ldrReference` with no location context. DetermineLocation answers the disclosed 501 (ADR-0191), so no
`ldrReference` is ever issued and every valid CancelLocation takes this path. The LMF's three other 404s
(Nlmf_DataExposure subscription GET/PUT/DELETE) already carry `SUBSCRIPTION_NOT_FOUND`. TS 29.572 (j70)
was fetched on 2026-11-20 (`specs/3gpp/MANIFEST.tsv`).

**Decision:**

- The answer becomes **403** with cause `LOCATION_SESSION_UNKNOWN`. TS 29.572 table 6.1.4.3.2-2
  (cancel-location responses) lists 204, 307, 308 and 403 only. The 403 row names `UNSPECIFIED` and
  `LOCATION_SESSION_UNKNOWN`, and table 6.1.7.3-1 defines the latter as "the location context was not
  found". This is the same change as the GMLC's (ADR-0599) and the AMF's `cancel-pos-info` (ADR-0588).
- Test: `LmfAuthorization.CancelLocationForUnknownLdrReferenceIs403LocationSessionUnknown` (real NRF and
  LMF, a valid token, a valid `CancelLocData` with `hgmlcCallBackURI` and `ldrReference`) asserts 403
  and the cause.
- `docs/TRACEABILITY.md`: the LMF CancelLocation row (ADR-0191 section) is annotated in place. The
  earlier live-curl `404` is kept as history.

**Verified.** Local, 2026-11-20, on the master machine: `integration_tests
--gtest_filter='LmfAuthorization.*'` passes (2 tests). With the `main.cpp` change reverted and `lmf`
rebuilt, the new test fails (`resp.status` is 404). Targeted test only, not a full `ctest`. CI result
to be added.

**Disclosed.**
- The YAML lists `'404'` for this operation through the common TS 29.500 table 5.2.7.1-1 set. The
  stage-3 table assigns an unknown location context to 403.
- No in-repo NF calls the LMF's `cancel-location`. The GMLC does not forward to an LMF (ADR-0189), so
  the status change has no in-repo consumer.
- After this change the LMF has no cause-less 404.

**Rejected alternatives.**
- *Keep 404 and add a cause.* TS 29.572 defines no 404 cause for this operation, so any cause would be
  invented (CLAUDE.md guardrail 1).
- *403 with `UNSPECIFIED`.* A less precise choice when the exact condition has its own cause.
