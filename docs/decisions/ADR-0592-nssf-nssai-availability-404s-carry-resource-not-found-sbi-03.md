## ADR-0592: NSSF NSSAI availability 404s carry RESOURCE_NOT_FOUND (SBI-03)

**Date:** 2026-11-19. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), NSSF part. The
NSSF had 2 cause-less 404s: PATCH and DELETE of `nssai-availability/{nfId}` for an nfId it holds no
NSSAI availability data for. TS 29.531 (j70) was fetched for this (`specs/3gpp/MANIFEST.tsv`).

**Decision:**

- Both 404s carry `RESOURCE_NOT_FOUND`: TS 29.531 tables 6.2.3.2.3.2-2 (PATCH) and 6.2.3.2.3.3-2
  (DELETE), and table 6.2.7.3-1 ("the resource related to the NF Id for which the NSSAI
  availability information is updated or deleted is unavailable").
- The subscription 404s already carried `SUBSCRIPTION_NOT_FOUND` and are unchanged.
- Test: `NssfAuthorization.NssaiAvailabilityDeleteOfUnknownNfIdIs404ResourceNotFound` (new, real
  nrf and nssf processes).

Disclosed:

- The PATCH cause has no test of its own; it is the same lookup and the same call as the DELETE.
- Compile-checked locally only (this host is a CI runner); CI on PR #42 passed: build, lint, asan-ubsan and tsan (2026-11-19).

**Rejected alternatives.**
- `SUBSCRIPTION_NOT_FOUND` or `CONTEXT_NOT_FOUND` on these 404s: TS 29.531 names
  `RESOURCE_NOT_FOUND` for this resource.
