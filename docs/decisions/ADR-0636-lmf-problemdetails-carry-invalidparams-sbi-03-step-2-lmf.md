## ADR-0636: LMF ProblemDetails carry invalidParams (SBI-03 step 2, LMF)

**Date:** 2026-11-26. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that takes an
`InvalidParam` list and builders for the `param` encodings of `TS29571_CommonData.yaml`
(`InvalidParam.param`: a JSON Pointer for a body attribute, `"query "` plus the name for a query
parameter, `"header "` plus the name for a header, `{name}` for a path variable). TS 29.500
V19.7.0 clause 5.2.7.2, Table 5.2.7.2-1 NOTE 1: "\"invalidParams\" attribute shall be included in
the \"ProblemDetails\" data structure indicating unsupported, missing or incorrect IE(s) or query
parameter(s) or 3gpp-Sbi-Discovery-* header(s)." The NOTE 1 causes are `INVALID_QUERY_PARAM`,
`MANDATORY_QUERY_PARAM_INCORRECT`, `OPTIONAL_QUERY_PARAM_INCORRECT`,
`MANDATORY_QUERY_PARAM_MISSING`, `MANDATORY_IE_INCORRECT`, `OPTIONAL_IE_INCORRECT`,
`MANDATORY_IE_MISSING` (400) and `MISSING_PARAMETER` (403). This ADR is step 2 for one NF, the LMF
(`nfs/lmf`), in the shape of ADR-0634 (GMLC) and ADR-0623 (NEF, whole-body pointer).

**Decision.** The LMF's two NOTE 1 sites now send invalidParams. A grep of `nfs/lmf/src` for the
NOTE 1 causes finds two sites, both in `nfs/lmf/src/main.cpp` (`TS29572_Nlmf_Location.yaml`):

| Route (operation) | Cause | invalidParams[].param |
|---|---|---|
| `POST /nlmf-loc/v1/determine-location` (DetermineLocation), a multipart/related body that cannot be parsed or has no part | `MANDATORY_IE_INCORRECT` | `""`, reason "malformed multipart/related body" |
| `POST /nlmf-loc/v1/configure-up` (ConfigureUp), `UpConfig` with neither `supi` nor `gpsi` | `MANDATORY_IE_MISSING` | `/supi` and `/gpsi`, reason "absent; one of supi and gpsi is required" |

- The multipart site names the whole body with the RFC 6901 pointer `""`: no part, so no
  `InputData` member, could be read, as ADR-0623 does for the NEF's JSON Patch bodies.
- The ConfigureUp site names both members of `UpConfig`'s `anyOf: [required: [supi], required:
  [gpsi]]`, since either one would satisfy it.
- No status code, cause, title or detail changed.
- Test: `LmfInvalidParams.*` in `tests/integration/test_lmf_invalid_params.cpp` (real nrf + lmf
  processes, TLS 1.3 + mTLS HTTP/2, a real NRF token for scope `nlmf-loc`):
  `MalformedMultipartDetermineLocationNamesTheWholeBody`,
  `ConfigureUpWithNeitherSupiNorGpsiNamesBoth` (each the exact cause and the exact invalidParams
  list) and `ConfigureUpWithSupiIsStored` (204, a positive control).

**Rejected alternatives.**
- Changing the multipart answer to `INVALID_MSG_FORMAT`: a malformed body is arguably that cause,
  not `MANDATORY_IE_INCORRECT`. Step 2 changes no status or cause; see Disclosed.
- Naming only `/supi` on ConfigureUp: a GPSI alone is accepted.

**Disclosed.**
- The multipart site's cause, `MANDATORY_IE_INCORRECT`, is kept as it was; whether it should be
  `INVALID_MSG_FORMAT` (which NOTE 1 does not cover) is left for a decision of its own.
- DetermineLocation's DTO parse failures use `dto_error_cause` and can be `MANDATORY_IE_MISSING` /
  `MANDATORY_IE_INCORRECT` without invalidParams: that is ADR-0607's disclosed sbi-core gap (the
  parser does not report where a member is nested), not this NF's.
- The `ecgi`-and-`ncgi` answer (`UNSPECIFIED_MSG_FAILURE`) is not a NOTE 1 cause and is unchanged.
  Errors answered by sbi-core before the handler (Authorizer, TPS limit, routing) are not part of
  this step.

**Verified, 2026-11-26, main checkout.** Tree: origin/main 9022f3a6 plus this change.
clang-format-18 on the changed C++ files. `cmake --build build --target integration_tests`, 0
failures. `--gtest_filter='Lmf*'` 5/5 pass (`LmfAuthorization.*` 2, `LmfInvalidParams.*` 3). With
`nfs/lmf/src/main.cpp` restored to origin/main and rebuilt, the two error-case tests fail
(invalidParams `{}`) and `ConfigureUpWithSupiIsStored`, the positive control, passes; the fix was
then restored and rebuilt. The master runner was idle (runner API `busy: false`). Only these
filters were run, not a full ctest.
