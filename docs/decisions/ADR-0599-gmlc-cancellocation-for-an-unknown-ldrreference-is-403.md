## ADR-0599: GMLC CancelLocation for an unknown ldrReference is 403 LOCATION_SESSION_UNKNOWN (SBI-03, GMLC part)

**Date:** 2026-11-20. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), GMLC part. The GMLC
had one cause-less 404: CancelLocation (`POST {apiRoot}/cancel-location`) for an `ldrReference` with
no location context. Since RequestLocation answers the disclosed 501 (ADR-0189), no `ldrReference` is
ever issued, so every valid CancelLocation takes this path. TS 29.515 (j60) was fetched on 2026-11-20
(`specs/3gpp/MANIFEST.tsv`).

**Decision:**

- The answer becomes **403** with cause `LOCATION_SESSION_UNKNOWN`. TS 29.515 table 6.1.3.3.2-2
  (cancel-location responses) lists 204, 307, 308 and 403 only. The 403 row names `UNSPECIFIED` and
  `LOCATION_SESSION_UNKNOWN`, and table 6.1.6.3-1 defines the latter as "the location context was not
  found". The 404 had no spec row and no cause. This matches the AMF's `cancel-pos-info`
  (ADR-0588, TS 29.518).
- Test: `GmlcAuthorization.CancelLocationForUnknownLdrReferenceIs403LocationSessionUnknown` (real NRF
  and GMLC, a valid token, a valid `CancelLocData`) asserts 403 and the cause.
- `docs/TRACEABILITY.md`: the GMLC CancelLocation row is annotated in place with the new status and
  test. The earlier live-curl `404` result is kept as history.

**Verified.** Local, 2026-11-20, on the master machine: `integration_tests
--gtest_filter='GmlcAuthorization.*'` passes (2 tests). With the `main.cpp` change reverted and `gmlc`
rebuilt, the new test fails (`resp.status` is 404). Targeted test only, not a full `ctest`. CI result
to be added.

**Disclosed.**
- The YAML (`TS29515_Ngmlc_Location.yaml`, `cancel-location`) also lists `'404'`, through the common
  response set every operation carries (TS 29.500 table 5.2.7.1-1 mandatory codes). That 404 is valid
  for an unmatched URI, not for an unknown location context, which the stage-3 table assigns to 403.
- After this change the GMLC has no cause-less 404.

**Rejected alternatives.**
- *Keep 404 and add a cause.* TS 29.515 defines no 404 cause for this operation, so any cause would be
  invented (CLAUDE.md guardrail 1).
- *403 with `UNSPECIFIED`.* A less precise choice when the exact condition, an unknown location
  context, has its own cause.
