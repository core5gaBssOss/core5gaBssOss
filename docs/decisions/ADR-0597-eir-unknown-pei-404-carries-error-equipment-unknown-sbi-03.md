## ADR-0597: EIR unknown-PEI 404 carries ERROR_EQUIPMENT_UNKNOWN (SBI-03, EIR part)

**Date:** 2026-11-20. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), EIR part. The 5G-EIR
had one cause-less 404: GetEquipmentStatus (`GET {apiRoot}/equipment-status?pei=...`) for a PEI it holds
no status for. TS 29.511 (j10) was fetched for this on 2026-11-20 (`specs/3gpp/MANIFEST.tsv`).

**Decision:**

- That 404 carries `ERROR_EQUIPMENT_UNKNOWN`. TS 29.511 clause 6.1.3.2.3.1 (GET response table) lists
  404 Not Found, "The equipment identify checking has failed", with that one application error, and
  table 6.1.5.3-1 defines it as "the mobile equipment is not known in the EIR".
- Test: `EirAuthorization.UnknownPeiIs404ErrorEquipmentUnknown` (real NRF and EIR processes, a valid
  token) asserts status 404 and the cause. It fails without the fix, since the body had no `cause`.

Disclosed:

- After this change the EIR has no 404 without a cause. Its other error responses (400
  `MANDATORY_QUERY_PARAM_MISSING`, the Authorizer's 401/403) were already conformant.
- The cause string is defined in the TS 29.511 text (table 6.1.5.3-1), not in the YAML, which lists
  the 404 response (`PEI Not Found`) with a plain `ProblemDetails`. That is the normal split for
  application errors.

**Verified.** Local, 2026-11-20, on the master machine: `integration_tests
--gtest_filter='EirAuthorization.*'` passes (2 tests). With the `main.cpp` change reverted and `eir`
rebuilt, `EirAuthorization.UnknownPeiIs404ErrorEquipmentUnknown` fails (`cause` is `""`). Targeted
test only, not a full `ctest`. CI result to be added.

**Rejected alternatives.**
- A generic TS 29.500 cause (`RESOURCE_URI_STRUCTURE_NOT_FOUND`): it is for an unmatched URI path
  segment, not an unknown query value. The API-specific cause applies.
