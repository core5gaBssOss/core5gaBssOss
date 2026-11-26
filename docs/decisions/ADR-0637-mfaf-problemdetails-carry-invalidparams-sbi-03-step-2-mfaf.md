## ADR-0637: MFAF ProblemDetails carry invalidParams (SBI-03 step 2, MFAF)

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
`MANDATORY_IE_MISSING` (400) and `MISSING_PARAMETER` (403). This ADR is step 2 for one NF, the
MFAF (`nfs/mfaf`), in the shape of ADR-0634 (GMLC) and ADR-0623 (NEF, whole-body pointer).

**Decision.** The MFAF's four NOTE 1 sites now send invalidParams. A grep of `nfs/mfaf/src` for the
NOTE 1 causes finds four sites, all `MANDATORY_IE_MISSING` in `nfs/mfaf/src/main.cpp`:

| Route (operation, YAML) | Condition | invalidParams[].param |
|---|---|---|
| `POST /nmfaf-3dadatamanagement/v1/configurations`, `PUT .../{transRefId}` (Configure / update, `TS29576_Nmfaf_3daDataManagement.yaml`) | `MfafConfiguration` with neither member of its `oneOf` (an empty `messageConfigurations` counts as absent, `minItems: 1`) | `/messageConfigurations` and `/mfafTransferInfo`, reason "absent; one of messageConfigurations and mfafTransferInfo is required" |
| same | a `MessageConfiguration` whose required `notificationURI` and/or `correId` is empty | `/messageConfigurations/{i}/notificationURI`, `/messageConfigurations/{i}/correId` (each empty one), reason "empty" |
| `POST /nmfaf-contextmanagement/v1/transfer` (Transfer, `TS29576_Nmfaf_ContextManagement.yaml`) | `refIds` empty (`minItems: 1`) | `/refIds`, reason "empty" |
| `POST /mfaf-inbound/v1/fetch` (the fetch callback, `TS29576_Nmfaf_3caDataManagement.yaml`) | the id array is empty (`minItems: 1`) | `""` (the whole body), reason "empty" |

- The local `problem` helper gains an overload that takes the `InvalidParam` list and calls
  `sbi_core::make_problem_details` with it.
- The `oneOf` site's other branch, both members present, stays `INVALID_MSG_FORMAT` (not a NOTE 1
  cause) without invalidParams.
- No status code, cause, title or detail changed.
- Test: `MfafInvalidParams.*` in `tests/integration/test_mfaf_invalid_params.cpp` (real nrf + mfaf
  processes, TLS 1.3 + mTLS HTTP/2, real NRF tokens for `nmfaf-3dadatamanagement` and
  `nmfaf-contextmanagement`; the fetch callback takes none): one test per site, each asserting the
  exact cause and the exact invalidParams list, and `ValidConfigurationIsCreated` (201), a positive
  control. Skipped under ThreadSanitizer, as `test_mfaf.cpp` is.

**Rejected alternatives.**
- Naming only the first empty `MessageConfiguration` member: both can be empty in one entry, and
  NOTE 1 asks for the incorrect IE(s).
- Changing the empty-string and empty-array answers to `MANDATORY_IE_INCORRECT`: the member is
  present, so that cause would arguably fit better, but step 2 changes no status or cause; see
  Disclosed.

**Disclosed.**
- An empty `notificationURI` / `correId` / `refIds` / id array is present in the body, yet it is
  answered `MANDATORY_IE_MISSING`. The cause is kept; whether these should be
  `MANDATORY_IE_INCORRECT` is left for a decision of its own. Only the first
  `MessageConfiguration` with an empty required member is reported (the loop returns there).
- Absent required members are DTO parse failures (`dto_error_cause`) without invalidParams:
  ADR-0607's disclosed sbi-core gap, not this NF's.
- The `mfafTransferInfo` answers (`INVALID_MSG_FORMAT`) are not NOTE 1 causes and are unchanged.
  Errors answered by sbi-core before the handler are not part of this step.

**Verified, 2026-11-26, main checkout.** Tree: origin/main 9022f3a6 plus this change.
clang-format-18 on the changed C++ files. `cmake --build build --target integration_tests`, 0
failures. Valkey (`valkey-local`) running, no Kafka broker.
`--gtest_filter='MfafInvalidParams.*:DccfAuthorization.*:MfafAuthorization.*'` 7/7 pass. With
`nfs/mfaf/src/main.cpp` restored to origin/main and rebuilt, the four error-case tests fail (each
with invalidParams `{}`) and `ValidConfigurationIsCreated`, the positive control, passes; the fix
was then restored and rebuilt. The master runner was idle (runner API `busy: false`). Only these
filters were run, not a full ctest.
