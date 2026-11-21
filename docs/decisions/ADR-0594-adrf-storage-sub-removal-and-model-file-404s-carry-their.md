## ADR-0594: ADRF storage-sub-removal and model-file 404s carry their cause (SBI-03)

**Date:** 2026-11-20. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), ADRF part. The
ADRF had 4 cause-less 404s. TS 29.575 (j70) is in `specs/3gpp/MANIFEST.tsv`.

**Decision:**

- Nadrf_DataManagement `request-storage-sub-removal` for a transRefId or dataSetId with no storage
  subscription: `SUBSCRIPTION_NOT_FOUND`. TS 29.575 table 5.1.7.3-1 (Nadrf_DataManagement
  application errors) is empty; the cause is the common one of TS 29.500 table 5.2.7.2-1 ("the
  request for modification or deletion of a subscription ... is rejected because the subscription
  is not found"). TS 29.575 clause 5.1.7.3 itself only points to its (empty) table.
- GET `/adrf-mlmodel-files/v1/{modelUniqueId}` with a non-numeric id: `ML_MODEL_NOT_FOUND`, the
  cause the same handler already sends for an unknown numeric id. This path is the ADRF's own ML
  model file address, not a 3GPP API.
- Test: `test_adrf.cpp` asserts the cause on the second removal of the same transRefId.

Unchanged, because the spec defines no cause for them:

- DELETE `data-store-records/{storeTransId}` for an unknown record, and a `targetNfId` that is not
  registered at the NRF: Nadrf_DataManagement defines no 404 cause, and TS 29.500 clause 5.2.7.2
  answers an unknown resource with a plain 404.

Disclosed:

- The model-file cause has no test of its own.
- Compile-checked locally only (this host is a CI runner); the PR's sanitizer runs were cancelled after merge, before they finished (build and lint passed). On `main`: asan-ubsan passed at fdbeb4af and tsan at cc995945 (2026-11-20, runs 44763962285 and 44791974533), both containing this change.

**Rejected alternatives.**
- `DATA_NOT_FOUND` or `CONTEXT_NOT_FOUND` on the storage-subscription removal: neither is defined
  for Nadrf_DataManagement, and the request is the deletion of a subscription.
