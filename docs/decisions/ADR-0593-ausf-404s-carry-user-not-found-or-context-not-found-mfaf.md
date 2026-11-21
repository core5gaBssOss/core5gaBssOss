## ADR-0593: AUSF 404s carry USER_NOT_FOUND or CONTEXT_NOT_FOUND; MFAF and CHF need no cause (SBI-03)

**Date:** 2026-11-20. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), AUSF part. The
AUSF had 11 cause-less 404s; 9 now carry a cause. TS 29.509 (j60) was fetched for this (`specs/3gpp/MANIFEST.tsv`). The
MFAF (TS 29.576) and CHF (TS 32.291) 404s were checked in the same pass.

**Decision:**

- Nausf_UEAuthentication POST `ue-authentications` and POST `prose-authentications`, when the UDM
  answers 404: `USER_NOT_FOUND` (TS 29.509 tables 6.1.3.2.3.1-3 and 6.1.3.6.3.1-3).
- POST `ue-authentications/deregister` with no context for the SUPI: `CONTEXT_NOT_FOUND` (table
  6.1.3.2.4.2.2-2).
- The 6 404s for an unknown `{authCtxId}` (5g-aka-confirmation PUT and DELETE, eap-session POST and
  DELETE, prose-auth POST and DELETE): `CONTEXT_NOT_FOUND`. Their per-method tables list no 404
  row; the API's table 6.1.7.3-1 defines `CONTEXT_NOT_FOUND` as "the AUSF cannot found the
  resource corresponding to the URI provided by the NF Service Consumer", which is this case.
- Tests in `test_ausf_ue_authentication.cpp` assert the cause on the second 5g-aka-confirmation
  DELETE, the second deregister and the unknown-SUPI authentication.

Unchanged, because the spec defines no 404 cause:

- AUSF Nausf_SoRProtection and Nausf_UPUProtection 404s for a SUPI with no KAUSF: TS 29.509 tables
  6.2.7.3-1 and 6.3.7.3-1 define only `COUNTER_WRAP` (503).
- MFAF: the 4 404s (configuration PUT and DELETE, context transfer, fetch). TS 29.576 tables
  5.1.7.3-1, 5.2.7.3-1 and 5.3.7.3-1 are empty.
- CHF: the 4 404s for an unknown charging data ref (Nchf_ConvergedCharging update and release,
  Nchf_OfflineOnlyCharging update and release). TS 32.291 table 6.1.7.3-1 defines `USER_UNKNOWN`
  for an end user the CHF cannot serve, not for an unknown resource; TS 29.500 clause 5.2.7.2
  answers an unknown resource with a plain 404. The spending-limit 404s already carry
  `SUBSCRIPTION_NOT_FOUND`.

Disclosed:

- `CONTEXT_NOT_FOUND` on the `{authCtxId}` 404s rests on table 6.1.7.3-1, not on a per-method
  row. The eap-session and prose-auth causes have no test of their own.
- Compile-checked locally only (this host is a CI runner); CI on PR #43 passed: build, lint, asan-ubsan and tsan (2026-11-19).

**Rejected alternatives.**
- Leaving the `{authCtxId}` 404s cause-less because their per-method tables have no 404 row: the
  API's own application-error table names the cause for exactly this case.
- `USER_UNKNOWN` on the CHF unknown-ref 404s: it describes a different failure.
