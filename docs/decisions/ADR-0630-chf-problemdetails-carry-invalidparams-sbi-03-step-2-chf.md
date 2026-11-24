## ADR-0630: CHF ProblemDetails carry invalidParams (SBI-03 step 2, CHF)

**Date:** 2026-11-24. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that takes
`std::vector<sbi_core::InvalidParam>` and four builders for the `param` encodings of
`TS29571_CommonData.yaml`. Step 2 moves each NF's own NOTE 1 call sites onto it, one NF per PR
(UDSF ADR-0608, UDR ADR-0609, SEPP ADR-0610). TS 29.500 V19.7.0, Table 5.2.7.2-1, says:

> NOTE 1:"invalidParams" attribute shall be included in the "ProblemDetails" data structure
> indicating unsupported, missing or incorrect IE(s) or query parameter(s) or 3gpp-Sbi-Discovery-*
> header(s).

The CHF (`nfs/chf/src/main.cpp`) names a NOTE 1 cause as a literal at exactly two sites, both on
this project's own routes, not on an Nchf operation:

- `POST /internal-billing/v1/runs` (ADR-0311): `MANDATORY_IE_MISSING` when one of
  `billingAccountId`, `billNo`, `periodStart`, `periodEnd` is absent or not a string.
- `PUT /chf-admin/v1/policy-counters/{policyCounterId}` (ADR-0072): `MANDATORY_IE_INCORRECT` when
  `currentStatus` is absent or not a string.

The Nchf_ConvergedCharging, Nchf_OfflineOnlyCharging and Nchf_SpendingLimitControl routes answer
their body errors through `sbi_core::http2::parse_json_body`, which ADR-0607 left without
invalidParams (the nlohmann exception carries no JSON Pointer). Their other errors are 404
(`std::nullopt` or `SUBSCRIPTION_NOT_FOUND`), 504 and `INVALID_MSG_FORMAT`, none in the NOTE 1 set.
The CHF answers no `MISSING_PARAMETER` (403) and no query-parameter cause.

**Decision:**

- `main.cpp:1036` (bill run): the 400 `MANDATORY_IE_MISSING` carries one InvalidParam, the JSON
  Pointer of the member the loop stopped at (`/billingAccountId`, `/billNo`, `/periodStart` or
  `/periodEnd`), reason `missing` when the member is absent and `must be a string` when it is
  present with another type.
- `main.cpp:1961` (policy counter): the 400 `MANDATORY_IE_INCORRECT` carries `/currentStatus`, with
  the same two reasons.
- Status, title, detail and cause are unchanged at both sites. No rating, charging, CDR or balance
  code is touched.
- Test: `ChfInvalidParams.*` in `tests/integration/test_chf_invalid_params.cpp` (real nrf + chf over
  mTLS with an NRF token; scope `internal-billing` or `chf-admin`, aud CHF), four probes, each
  asserting status 400, the exact cause, the exact `param` list and the reason. Both sites are
  reached: the handlers' own `json::parse` accepts the probe bodies, and nothing runs before the
  member check. The two routes do not shadow each other or any Nchf route (different methods and
  prefixes).

**Rejected alternatives.**

- *Change the bill run's cause to `MANDATORY_IE_INCORRECT` for a present, non-string member.*
  Arguably the better cause, but this step changes no cause or status; recorded below instead.
- *List every absent bill-run member, not only the first.* The handler returns at the first one;
  collecting all would change the control flow and the `detail` text, beyond this step.
- *Name the member in the Nchf routes' `parse_json_body` failures.* ADR-0607 rejected deriving a
  pointer from the exception message (wrong for nested members); unchanged here.

**Disclosed.**

- The two changed sites are project-owned routes (`/internal-billing/v1`, `/chf-admin/v1`), not
  3GPP APIs; they use TS 29.500 ProblemDetails and causes, so NOTE 1 is applied to them as written.
- The bill run answers `MANDATORY_IE_MISSING` also when the member is present but not a string;
  Table 5.2.7.2-1's text for that cause is "is not included in the request". Left as is (doubtful
  cause, not changed by this step); the reason `must be a string` says what happened.
- `MANDATORY_IE_INCORRECT` for a non-string `currentStatus` is a type error, not a "semantically
  incorrect value"; left as is.
- A body that is valid JSON but not an object (e.g. `[]`) answers the same 400 naming
  `/billingAccountId` or `/currentStatus` as missing; the pointer is then relative to a non-object
  root. Not probed.
- The Nchf routes' `parse_json_body` 400s (`MANDATORY_IE_MISSING` / `MANDATORY_IE_INCORRECT` from a
  failing generated `from_json`) still carry no invalidParams (ADR-0607's remaining gap).
- Verified: clang-format-18 `--dry-run -Werror` on the changed files and a `-fsyntax-only` compile
  of `nfs/chf/src/main.cpp` and `tests/integration/test_chf_invalid_params.cpp`. Not verified: no
  full build and no run of `integration_tests`, which needs the lab certificates and, for the CHF at
  startup, Valkey/Redis (`redis_url`, 127.0.0.1:6379), Doris FE (127.0.0.1:9030) with database
  `chf_cdr` (direct-insert CDR mode is the default and exits if Doris is unreachable) and the rating
  PostgreSQL (`postgresql://postgres@127.0.0.1:5434/charging`). `chf_features` is not a CHF startup
  need (the NWDAF's feature store).

**Verified by the lead, 2026-11-24, main checkout (runner 23 idle).** Tree: origin/main 4cb7a62e plus #79 (NSSF), #80 (BSF) and #81 (CHF) applied together; the only conflicts were the test files appended to the integration_tests list, resolved by keeping all three. Full build. `NssfInvalidParams.*` 4/4, `BsfInvalidParams.*` 3/3 and `ChfInvalidParams.*` 4/4 pass. With nfs/nssf, nfs/bsf and nfs/chf main.cpp restored to main and rebuilt, the 10 error-case tests fail; `NssfInvalidParams.BothPresentReachesTheSelection` is a positive control and passes either way. Regression: `--gtest_filter='Nssf*:Bsf*:Chf*'` 22/22 pass. Local Doris had chf_cdr applied. Only these filters were run, not a full ctest.
