## ADR-0624: ADRF ProblemDetails carry invalidParams (SBI-03 step 2, ADRF)

**Date:** 2026-11-23. **Status:** Proposed.

**Context.** ADR-0607 (step 1) gave sbi-core a way to put `invalidParams` in a ProblemDetails and
four builders for the `InvalidParam.param` encodings of `TS29571_CommonData.yaml`. Step 2 is one
NF per PR (UDR ADR-0609, SEPP ADR-0610, UDSF ADR-0608, ...). This ADR is the ADRF
(`nfs/adrf`, TS 29.575). TS 29.500 V19.7.0 (`specs/3gpp/29500-j70.docx`), Table 5.2.7.2-1:

> NOTE 1: "invalidParams" attribute shall be included in the "ProblemDetails" data structure
> indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
> 3gpp-Sbi-Discovery-* header(s).

The NOTE 1 causes are `INVALID_QUERY_PARAM`, `MANDATORY_QUERY_PARAM_INCORRECT`,
`OPTIONAL_QUERY_PARAM_INCORRECT`, `MANDATORY_QUERY_PARAM_MISSING`, `MANDATORY_IE_INCORRECT`,
`OPTIONAL_IE_INCORRECT`, `MANDATORY_IE_MISSING` (400) and `MISSING_PARAMETER` (403). Before this
change none of the ADRF's own answers with one of these causes carried `invalidParams`.

**Decision:** every ADRF answer whose cause is a NOTE 1 cause, built in `nfs/adrf/src/main.cpp`,
now carries `invalidParams` naming the IE or query parameter. Status codes, causes and details are
unchanged. The local `problem()` helper takes an optional `std::vector<sbi_core::InvalidParam>`
(empty leaves the attribute out, ADR-0607). A new helper `one_of_absent(members)` names every
member of a oneOf / anyOf of which none is present, as a JSON Pointer, with one reason
("absent; one of a and b is required") -- the shape SEPP (ADR-0610) and NEF (ADR-0623) use for the
same case. Names are checked against `TS29575_Nadrf_DataManagement.yaml` and
`TS29575_Nadrf_MLModelManagement.yaml` (REL-19, commit bca84b6).

| Route (operation) | Cause | invalidParams |
|---|---|---|
| GET `/data-store-records` (RetrievalRequest), none or more than one selector | MANDATORY_IE_MISSING | none given: `query store-trans-id`, `query fetch-correlation-ids`, `query data-set-id`; more than one: the ones given |
| POST `/request-storage-sub`, neither `anaSub` nor `dataSub` | MANDATORY_IE_MISSING | `/anaSub`, `/dataSub` |
| POST `/request-storage-sub`, neither `targetNfId` nor `targetNfSetId` | MANDATORY_IE_MISSING | `/targetNfId`, `/targetNfSetId` |
| POST `/request-storage-sub-removal`, neither `transRefId` nor `dataSetId` | MANDATORY_IE_MISSING | `/transRefId`, `/dataSetId` |
| POST `/data-retrieval-subscriptions`, none of `anaSub`, `dataSub`, `dataSetId` | MANDATORY_IE_MISSING | `/anaSub`, `/dataSub`, `/dataSetId` |
| POST `/mlmodel-store-records`, PUT `/mlmodel-store-records/{storeTransId}`, neither `nfInstanceId` nor `nfSetId` | MANDATORY_IE_MISSING | `/nfInstanceId`, `/nfSetId` |
| same, neither `mlModelInfo` nor `mlModels` | MANDATORY_IE_MISSING | `/mlModelInfo`, `/mlModels` |
| same, an `MLModel` without `modelUniqueId` / `mlModel` | MANDATORY_IE_MISSING | `/mlModels/<i>/<member>`, reason `missing`, one entry per missing member |
| same, an `MLModelInfo` without `modelUniqueId` / `mlFileAddr` | MANDATORY_IE_MISSING | `/mlModelInfo/<i>/<member>`, reason `missing` |
| GET `/mlmodel-store-records` (RetrievalRequest), both or neither selector | MANDATORY_IE_MISSING | `query store-trans-id`, `query model-unique-ids` |
| same, a `model-unique-ids` item that is not an integer | INVALID_QUERY_PARAM | `query model-unique-ids`, reason `'<item>' is not an integer` |
| POST `/remove-stored-mlmodel`, empty array (`minItems: 1`) | MANDATORY_IE_MISSING | `""` (the JSON Pointer of the whole body, which is the array; NEF ADR-0623 uses the same) |

The MLModel / MLModelInfo checks are reachable: the generated DTO members are all
`std::optional` (the YAML requires them through `allOf`), so `parse_json_body` accepts the body
and the handler's own check answers.

Tests: `AdrfInvalidParams.*` (13 tests) in `tests/integration/test_adrf_invalid_params.cpp`,
`integration_tests` binary, one probe per row above (both halves of the first), asserting the exact
`invalidParams` array (param and reason) and the cause. Real `nrf` + `adrf` processes; the ADRF
only starts with Apache Doris, its model-store PostgreSQL (127.0.0.1:5438) and Valkey reachable
(fail-fast), the same needs as `test_adrf.cpp`. Every probe is answered by request validation
before any store is read. Readiness uses `nf_test::wait_until_ready` (ADR-0617): ready once a
tokened bare RetrievalRequest returns 400. No route probed is shadowed: sbi-core matches routes
segment by segment, and the probed paths share no pattern with an earlier route of the same
method.

**Sites left without invalidParams.**

- The `MANDATORY_IE_MISSING` / `MANDATORY_IE_INCORRECT` answers of `sbi_core::http2::parse_json_body`
  (a generated `from_json` throwing). ADR-0607 left these in sbi-core: the exception does not carry
  the JSON Pointer of the failing member. This is the sbi-core gap ADR-0607 discloses, not an ADRF
  site.
- The answers of the other oneOf half on the same routes ("both present") use
  `INVALID_MSG_FORMAT`, which NOTE 1 does not cover; they are unchanged and carry no
  `invalidParams`.

**Disclosed.**

- Causes this PR keeps although they look wrong (no cause or status was changed here):
  - GET `/data-store-records` and GET `/mlmodel-store-records` answer `MANDATORY_IE_MISSING` for
    a missing or surplus query parameter. Table 5.2.7.2-1 has `MANDATORY_QUERY_PARAM_MISSING`
    (none given) and `INVALID_QUERY_PARAM` (more than one given) for query parameters. The
    invalidParams entries already use the `query ` encoding, so a later cause fix changes only the
    cause.
  - "Both present" on GET `/mlmodel-store-records` shares the `MANDATORY_IE_MISSING` cause with
    "both absent"; the reason text tells them apart.
  - `timePeriod is not a TimeWindow` (remove-stored-data-analytics, data-retrieval-subscriptions)
    answers `INVALID_MSG_FORMAT`; `MANDATORY_IE_INCORRECT` with `/timePeriod` may fit better.
    Unchanged, so no `invalidParams`.
  - The `targetNfId ... is a <type>` answer is `UNSPECIFIED_MSG_FAILURE` (not NOTE 1). Unchanged.
- `MLModelInfo` also requires `mlStorageSize` in the YAML (`allOf`). The ADRF's request check does
  not test it (it never did); this PR names only the members the existing check tests.
- The anyOf `mlModelInfo` / `mlModels` reason says "one of ... is required", the same text as for a
  oneOf, although both may be present.
- Not verified here: no build of the binaries and no test run. Checked: `clang-format-18
  --dry-run -Werror` on both changed C++ files, and a syntax-only compile of
  `nfs/adrf/src/main.cpp` and `tests/integration/test_adrf_invalid_params.cpp` with the flags of
  the main checkout's `compile_commands.json`. The integration tests need `certs/` and the Doris /
  PostgreSQL / Valkey lab; CI runs them.

**Rejected alternatives.**

- Naming only the first member of a oneOf of which none is present (e.g. only `/anaSub`). Rejected:
  the consumer may supply either, so naming one would say the other is not an option.
- Changing the query-parameter causes to `MANDATORY_QUERY_PARAM_MISSING` / `INVALID_QUERY_PARAM`
  in the same PR. Rejected: step 2 adds `invalidParams` only; a cause change is a separate,
  reviewable decision (disclosed above).
- Adding `invalidParams` to the `INVALID_MSG_FORMAT` answers too. Rejected: NOTE 1 does not cover
  that cause, and the step 2 PRs for other NFs leave it alone.
- A pointer per array element of `remove-stored-mlmodel` (`/0`). Rejected: the array is empty, so
  there is no element; the whole body (`""`) is what violates `minItems: 1`.

**Verified by the lead, 2026-11-23, main checkout (runner 23 idle).** Tree: origin/main 9119d75f plus #75 (ADRF) and #76 (NWDAF) applied together; the only conflict was both test files appended to the integration_tests list, resolved by keeping both. Full build. Containers: Valkey, the ADRF model-store Postgres on 5438 (nfs/adrf/schema.sql) and Apache Doris 4.1.3 with the `chf_features` and `adrf_data` schemas applied as CI does. The first run failed only because Doris was still starting. `AdrfInvalidParams.*` 13/13 and `NwdafInvalidParams.*` 8/8 pass. With nfs/adrf/src/main.cpp, nfs/nwdaf/src/main.cpp and nfs/nwdaf/src/mtlf.cpp restored to main and rebuilt, all 21 fail. Regression: `--gtest_filter='Adrf*:Nwdaf*'` 38 tests: 35 pass, 3 skipped (the DCCF-dependent Adrf.StorageSubscriptionThroughTheDccfStoresSourceData and two NwdafPhaseC tests), none fail. Only these filters were run, not a full ctest.
