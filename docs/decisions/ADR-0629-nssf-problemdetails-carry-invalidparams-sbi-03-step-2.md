## ADR-0629: NSSF ProblemDetails carry invalidParams (SBI-03 step 2)

**Date:** 2026-11-23. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that carries
`invalidParams` and the four `InvalidParam.param` builders. TS 29.500 V19.7.0, clause 5.2.7.2,
Table 5.2.7.2-1, NOTE 1:

> "invalidParams" attribute shall be included in the "ProblemDetails" data structure indicating
> unsupported, missing or incorrect IE(s) or query parameter(s) or 3gpp-Sbi-Discovery-* header(s).

`TS29571_CommonData.yaml` `InvalidParam.param`: a query parameter is "query " plus its name. This
ADR is step 2 for the NSSF only (`nfs/nssf`, TS 29.531). A survey of `nfs/nssf/src` finds three
places that can answer a NOTE 1 cause:

1. NSSelectionGet (`GET /nnssf-nsselection/v2/network-slice-information`) without `nf-type` or
   `nf-id`: 400 `MANDATORY_QUERY_PARAM_MISSING`. Both are `required: true` in
   `TS29531_Nnssf_NSSelection.yaml`.
2. NSSAIAvailabilityPost and NSSAIAvailabilityPut, through `sbi_core::http2::parse_json_body`:
   400 `MANDATORY_IE_MISSING` / `MANDATORY_IE_INCORRECT` (`dto_error_cause`).
3. NSSAIAvailabilityPatch, when the patched document does not read back into
   `NssaiAvailabilityInfo`: 400 with `dto_error_cause(e)`.

**Decision:**

- Site 1 (`nfs/nssf/src/main.cpp`, the NSSelectionGet handler) now lists each absent one of
  `nf-type` / `nf-id` as `query nf-type` / `query nf-id`, built with
  `sbi_core::invalid_query_param`; a present one is not listed. Status, cause, title and detail are
  unchanged.
- Test: `NssfInvalidParams.*` in `tests/integration/test_nssf_invalid_params.cpp` (registered last
  in `integration_tests`): neither parameter (both listed, in that order), only `nf-type` (`query
  nf-id`), only `nf-id` (`query nf-type`), and both present (200, so the check does not reject a
  valid request). Each 400 checks the exact cause and the exact `param` list. Real `nrf` + `nssf`
  processes; readiness through `nf_test::wait_tcp_listening` and `nf_test::wait_until_ready`
  (ADR-0617).

**Left unchanged, and why.**

- Site 2: `parse_json_body` is out of scope per ADR-0607 (the nlohmann exception text carries no
  member path, so the failing IE cannot be named honestly). A remaining NOTE 1 gap, owned by a
  later sbi-core step, not by the NSSF.
- Site 3: the same reason. The failing member of the patched `NssaiAvailabilityInfo` is not known
  from `json::exception`; naming the whole body (`""`) would claim the request body was wrong when
  it was the result of applying it. A remaining NOTE 1 gap.

**Disclosed.**

- Malformed JSON-encoded query parameters are not answered with an error at all:
  `get_json_query_param` returns `std::nullopt` for a `slice-info-request-for-*` value that is not
  JSON or does not read into its DTO, and the handler then treats it as absent (falls back to the
  catalog). That is not a NOTE 1 site today (no NOTE 1 cause is sent), so it is not changed here;
  answering 400 `MANDATORY_QUERY_PARAM_INCORRECT` / `OPTIONAL_QUERY_PARAM_INCORRECT` with
  `query <name>` would be a behaviour change and needs its own decision. Same for `home-plmn-id`,
  `tai` and `supported-features`, which the handler does not read.
- `nf-type` and `nf-id` are checked for presence only; an `nf-type` that is not an `NFType` value
  or an `nf-id` that is not an `NfInstanceId` UUID is accepted. Not changed here (no error is sent
  today).
- An empty value (`?nf-type=&nf-id=`) counts as present, as before.
- `reason` is left out (optional in the YAML); the existing `detail` text already says both are
  required.

**Verified (2026-11-23, agent worktree).** `clang-format-18 --dry-run -Werror` is clean on
`nfs/nssf/src/main.cpp` and `tests/integration/test_nssf_invalid_params.cpp`. A syntax-only compile
(`-fsyntax-only`, flags from the main checkout's `build/compile_commands.json`) of both TUs passes
with no warning on a changed line. **Not done:** no build, no run of `NssfInvalidParams.*` (the
fixture needs the lab certificates in `certs/`, which this session does not use). To be run by the
reviewer; CI result to be added.

**Verified by the lead, 2026-11-24, main checkout (runner 23 idle).** Tree: origin/main 4cb7a62e plus #79 (NSSF), #80 (BSF) and #81 (CHF) applied together; the only conflicts were the test files appended to the integration_tests list, resolved by keeping all three. Full build. `NssfInvalidParams.*` 4/4, `BsfInvalidParams.*` 3/3 and `ChfInvalidParams.*` 4/4 pass. With nfs/nssf, nfs/bsf and nfs/chf main.cpp restored to main and rebuilt, the 10 error-case tests fail; `NssfInvalidParams.BothPresentReachesTheSelection` is a positive control and passes either way. Regression: `--gtest_filter='Nssf*:Bsf*:Chf*'` 22/22 pass. Local Doris had chf_cdr applied. Only these filters were run, not a full ctest.

**Rejected alternatives.**

- *Always list both `query nf-type` and `query nf-id` when either is missing.* The handler knows
  which one is absent; listing a present parameter as missing would be wrong.
- *Turn a malformed `slice-info-request-for-*` into a 400 in this PR.* A status change, outside the
  "invalidParams only" scope of step 2 (see Disclosed).
- *Name `""` for the PATCH site.* See site 3 above: misleading.
