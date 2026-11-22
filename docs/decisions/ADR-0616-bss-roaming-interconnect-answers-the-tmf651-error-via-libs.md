## ADR-0616: bss/roaming-interconnect answers the TMF651 Error via libs/tmf-core (BSS SBI-03 PR 3)

**Date:** 2026-11-22. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SBI-03 for the BSS, PR 3 of the series ADR-0612 describes: one
service moves onto the shared TM Forum `Error` helper. `bss/roaming-interconnect` serves one API,
TM Forum TMF651 Agreement Management, at `/tmf-api/agreementManagement/v4` (its `src/main.cpp`
header). It has three routes: `POST /agreement`, `GET /agreement` and `GET /agreement/{id}`. It
serves no project-internal (`/bss-api/...`) API, so ADR-0612's Q5 has nothing to apply to here.

In `specs/tmforum/TMF651-Agreement-v4.0.0.swagger.json` (vendored by ADR-0611), every 4xx/5xx
response of every operation has the schema `#/definitions/Error` (lines 3351-3394). That
definition is identical to TMF640's, which libs/tmf-core builds. `produces` is
`application/json;charset=utf-8`. `POST /agreement` declares 400, 401, 403, 405, 409 and 500 (no
404). `GET /agreement/{id}` declares 400, 401, 403, 404, 405, 409 and 500.

Before this ADR, the service's handlers answered three errors, all as 3GPP ProblemDetails
(`application/problem+json`):

- the `guarded` wrapper: a store `InvalidRequest` (ADR-0387) gave 400 `{"status":400,"title":
  "Bad Request","detail":<store text>}`;
- `sbi_core::http2::parse_json_body` on `POST /agreement`: a bad body gave 400 with the 3GPP
  cause `INVALID_MSG_FORMAT`, `MANDATORY_IE_MISSING` or `MANDATORY_IE_INCORRECT`, and a `detail`
  that was nlohmann's `what()`, which for a parse error quotes the input;
- `GET /agreement/{id}` for an unknown id gave 404 `{"status":404,"title":"Not Found",
  "detail":"No InterconnectAgreement <id>"}`.

There was no ad-hoc `{"error": ...}` body in the service.

**Decision:** The three handler errors answer the TMF `Error` through libs/tmf-core, with the
user's ADR-0612 choices (Q2 code = HTTP status as a string, Q3 reason = HTTP reason phrase and
message = the detail text, Q6 `application/json;charset=utf-8`). The statuses do not change.

- `bss/roaming-interconnect/src/tmf_errors.hpp` (new, header-only) holds `guarded`, moved out of
  `main.cpp`, and `agreement_not_found(id)`. `guarded` now answers
  `tmf_core::error_response(400, e.what())`. `agreement_not_found` answers
  `tmf_core::error_response(404, "No InterconnectAgreement " + id)`. The message texts are the
  ones the service already sent as `detail`.
- `POST /agreement` reads its body with `tmf_core::parse_json_body<InterconnectAgreement>`. A bad
  body is a 400 TMF Error with no cause and a fixed message that does not echo the input.
- `roaming-interconnect` links `tmf_core`. `main.cpp` no longer includes
  `sbi_core/json_body.hpp`.
- Tests: `tests/conformance/test_roaming_interconnect_tmf_error.cpp`, 7
  `RoamingInterconnectTmfError.*` cases in `conformance_tests`. They drive `guarded`,
  `agreement_not_found` and `parse_json_body<InterconnectAgreement>` and check the body against
  the Error definition read from the vendored swagger at test time: required members present,
  every member declared, string members strings, `code` and `status` equal to the status digits,
  the exact content type, and no `cause`, `title`, `detail` or `invalidParams`. They also check
  that `guarded` passes a success through, that it leaves any other exception to the framework,
  and that a malformed body is not echoed back.

**Rejected alternatives.**

- **Keep the handlers inside `main()` and test only through a running server.** No HTTP-level test
  of this service exists, and one would need its certificates and a PostgreSQL. A header the
  routes and the conformance test both include tests the code the routes actually run.
- **A TMF651-specific `code` vocabulary.** TMF651 defines no code list either. Q2 applies.
- **Map the store's `InvalidRequest` texts to 404 or 409.** `a referenced entity does not exist`
  could arguably be 404 or 409, but this PR keeps the statuses. Changing one is a separate decision.
- **Read the Error definition from the TMF651 swagger in the test.** That file is vendored by
  ADR-0611 on a branch this PR is not cut from. The test reads TMF640's, which ADR-0611 records as
  identical. It can switch to TMF651 once ADR-0611 is merged.

**Disclosed.**

- The sbi-core framework's own errors on this server are still ProblemDetails: 404 for no route,
  405, 501, 413, the duplicate-member 400 (ADR-0354), the TPS ceiling's and the pool-exhaustion
  503s, and the 500 for an uncaught exception, which also carries the 3GPP cause `SYSTEM_FAILURE`.
  That is BSS SBI-03 PR 7 (ADR-0612 Q4). Of these, TMF651 declares 400, 404, 405 and 500. It
  declares no 413, 501 or 503.
- The store's `InvalidRequest` texts are forwarded as `message` as before. They are fixed strings
  plus a field name, never request data.
- The 404 message carries the requested id, as the old `detail` did.
- The 401 and 403 that TMF651 declares are never sent: the service has no OAuth2, only mTLS
  (its header's disclosed simplification).
- `tmf_errors.hpp` cites TMF651 swagger line numbers from ADR-0611's branch. Until that is merged,
  the file is not in this branch.

**Verified.** Measured on 2026-11-22. `clang-format-18 --dry-run -Werror` passes on the three
changed C++ files. A syntax-only compile of `src/main.cpp` with the flags of its
`compile_commands.json` entry, plus libs/tmf-core's include directory, passes. The new test file
was compiled and linked by hand into a standalone binary, with the main checkout's prebuilt
`roaming_interconnect_store`, `bss_sid`, `sbi_core`, `tap3_core` and `tmf_generated` archives (the
sources of those libraries are the same in this branch). 7 of 7 tests passed. With
`tmf_errors.hpp` put back to the old ProblemDetails bodies, the same binary failed the 400 and 404
cases (`StoreRejectionIs400TmfErrorWithStoreMessage`, `UnknownAgreementIs404TmfError`). The three
body-parse cases call `tmf_core::parse_json_body` directly, so they check the helper with this
service's DTO rather than the route's call site. The
`roaming-interconnect` and `conformance_tests` CMake targets were not built, and no `ctest` was
run.
