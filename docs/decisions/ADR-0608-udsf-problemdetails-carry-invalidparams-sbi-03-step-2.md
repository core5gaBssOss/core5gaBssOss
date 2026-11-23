## ADR-0608: UDSF ProblemDetails carry invalidParams (SBI-03 step 2)

**Date:** 2026-11-21. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` / `make_problem_details`
overload that carries `invalidParams`, and the four `InvalidParam.param` builders. No NF used them.
TS 29.500 V19.7.0, clause 5.2.7.2, Table 5.2.7.2-1, NOTE 1:

> "invalidParams" attribute shall be included in the "ProblemDetails" data structure indicating
> unsupported, missing or incorrect IE(s) or query parameter(s) or 3gpp-Sbi-Discovery-* header(s).

`TS29571_CommonData.yaml` `InvalidParam.param`: a JSON body attribute "shall be encoded as a JSON
Pointer"; a query parameter is "the concatenation of the string "query " plus the name of such
query parameter". This ADR is step 2, the UDSF only (`nfs/udsf`, TS 29.598). The query parameter
names below are the `name:` values in `TS29598_Nudsf_DataRepository.yaml` and
`TS29598_Nudsf_Timer.yaml` (specs/5G_APIs-REL-19); `metaTags` and `TagType.sort` are from
`MetaSchema` / `TagType` in the DataRepository YAML.

**Decision:**

- `udsf::problem` (`nfs/udsf/src/service.hpp`) gets a five-argument overload with a
  `std::vector<sbi_core::InvalidParam>`, forwarding to ADR-0607's `make_problem_details` overload.
  The four-argument overload is unchanged.
- A recount of the UDSF finds 19 call sites naming a NOTE 1 cause (`dr_routes.cpp` 15,
  `timer_routes.cpp` 4), as the step-1 survey said. 18 now carry invalidParams naming the
  parameter the code tested; status codes, causes, titles and details are unchanged:
  - SearchRecord: `query tag-count-filter` (unsupported feature), `query filter` (absent;
    unparseable, reason = the parser's text; not evaluable, reason = the evaluator's text),
    `query retrieve-records` (unknown value), `query count-indicator` + `query retrieve-records`
    (both given; the code rejects the pair, so both are named), `query limit-range` or
    `query max-payload-size` (whichever failed `query_uint`; reason = its text).
  - BulkDeleteRecords: `query filter` (absent, unparseable, not evaluable).
  - GetNotificationSubscriptions: `query limit-range`.
  - DeleteNotificationSubscription: `query client-id`, both when a JSON-encoded `client-id` does
    not parse and when neither form gives nfId or nfSetId.
  - CreateOrModifyMetaSchema: `MANDATORY_IE_MISSING` for an absent `TagType.sort` on a
    SEARCH_KEY / SEARCH_AND_COUNT_KEY tag names `/metaTags/<i>/sort`, built with
    `invalid_body_ie`. The index is the loop index over `ms.metaTags`, which was parsed from the
    whole request body, so it is the body's array index.
  - Nudsf_Timer SearchTimer and DeleteTimers (shared filter selection): `query filter` +
    `query expired-filter` (neither given), `query filter` (unparseable, a RecordIdList, not evaluable).
- Test: `Udsf.InvalidParamsNameTheParameter` in `tests/integration/test_udsf.cpp` checks the
  cause and `invalidParams[].param` for 12 requests covering both services, the query-parameter
  encoding, one reason text and the JSON Pointer site.

**Left unchanged, and why.**

- `dr_routes.cpp` `parse_record`, `MANDATORY_IE_MISSING` "every block part needs a Content-Id":
  the missing item is the Content-Id header of a multipart body part. It is not an HTTP request
  header ("header " encoding), not a JSON body attribute and not a query parameter or path
  variable, so none of the four `param` encodings fits. Inventing one is worse than leaving it;
  it is a remaining NOTE 1 gap.
- No UDSF site uses `parse_json_body`; the UDSF parses its bodies itself and answers
  `INVALID_MSG_FORMAT` for a body that does not read into the DTO, which NOTE 1 does not cover.

**Disclosed.**

- `client-id` is `schema: ClientId` with OpenAPI's default style=form, explode=true, so on the
  wire it is `nfId` / `nfSetId`, not `client-id` (the handler also accepts a JSON-encoded
  `client-id=`, ADR-0401). The `param` is the YAML parameter
  name `client-id`, which is what the YAML description asks for ("the name of such query
  parameter"); a consumer that keys on the wire names would not find `nfId` there.
- Where the code joins two parameters in one rejection (count-indicator with retrieve-records;
  filter or expired-filter missing) both are listed. TS 29.500 does not say whether a mutually
  exclusive pair is one invalid parameter or two.
- Reasons are filled only where the code already had a specific text (the filter parser and
  evaluator, `query_uint`); elsewhere `reason` is left out (optional in the YAML).

**Verified (2026-11-22, master machine, a fresh worktree build on top of ADR-0607's branch,
Debug).** `cmake --build build --target udsf integration_tests` ran 2779 of 2779 steps with 0
failed, and the build log has no warning on a changed line. The changed files are
clang-format-18 clean. `Udsf.InvalidParamsNameTheParameter` was **not run** here: the UDSF
integration fixture needs the lab certificates (`certs/`), which this session does not use. It is
to be run by the reviewer; CI result to be added. No other test was run.

**Verified by the lead (2026-11-22, main checkout, master machine with its CI runner idle).** One Debug build of ADR-0607's commit 55ba4d06 with the code and tests of PRs #60 (including the subs-to-notify route fix), #62 and #63 applied together. `udsf_integration_tests --gtest_filter='Udsf.*'` passes (13 of 13, `Udsf.InvalidParamsNameTheParameter` included). With `nfs/udsf/src/dr_routes.cpp` and `timer_routes.cpp` restored to 55ba4d06 and `udsf` rebuilt, `InvalidParamsNameTheParameter` fails. Targeted tests only, not a full `ctest`.

**Rejected alternatives.**
- *Name the multipart part's Content-Id as `header Content-Id`.* The YAML's "header " encoding is
  for an HTTP header of the request; a body part's MIME header is not one. Misleading.
- *Name only `retrieve-records` for the mutually exclusive pair.* The code does not prefer one of
  the two; naming one would claim knowledge it does not have.
- *Change `udsf::problem` to take a default empty vector.* Same reason as ADR-0607: a separate
  overload leaves every other call site's signature and output as they were.
