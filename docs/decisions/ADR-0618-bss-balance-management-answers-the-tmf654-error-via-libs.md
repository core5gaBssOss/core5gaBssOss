## ADR-0618: bss/balance-management answers the TMF654 Error via libs/tmf-core (BSS SBI-03 PR 4)

**Date:** 2026-11-22. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SBI-03 for the BSS, PR 4 of the series ADR-0612 describes: one
service moves onto the shared TM Forum `Error` helper. `bss/balance-management` serves one API,
TM Forum TMF654 Prepay Balance Management, at `/tmf-api/prepayBalanceManagement/v4` (its
`src/main.cpp` header). It has nine routes: `GET /bucket`, `GET /bucket/{id}`,
`GET /accumulatedBalance`, and `POST` plus `GET /{id}` for each of `topupBalance`,
`adjustBalance` and `reserveBalance`. It serves no project-internal (`/bss-api/...`) API, so
ADR-0612's Q5 has nothing to apply to here. `src/bill_run.cpp` (TMF678 bill run, ADR-0310) is a
library function with no HTTP route in this service.

In `specs/tmforum/TMF654-PrepayBalance-v4.0.0.swagger.json` (vendored by ADR-0611 on
`docs/vendor-tmf-swaggers`), `basePath` is `/tmf-api/prepayBalanceManagement/v4`, `produces` is
`application/json;charset=utf-8` (lines 16-18), and every 4xx/5xx response of every operation
this service routes has the schema `#/definitions/Error` (lines 5972-6015). That definition is
identical to TMF640's, which libs/tmf-core builds (compared as parsed JSON, 2026-11-22). The three
`POST` operations declare 400, 401, 403, 405, 409 and 500 (no 404). Every `GET` operation
declares 400, 401, 403, 404, 405, 409 and 500.

Before this ADR, the service's handlers answered these errors, all as 3GPP ProblemDetails
(`application/problem+json`):

- the `guarded` wrapper: a store `InvalidRequest` (ADR-0385: a malformed date-time; an unknown
  `partyAccount`) gave 400 `{"status":400,"title":"Bad Request","detail":<store text>}`;
- `sbi_core::http2::parse_json_body` on the three `POST` routes: a bad body gave 400 with the 3GPP
  cause `INVALID_MSG_FORMAT`, `MANDATORY_IE_MISSING` or `MANDATORY_IE_INCORRECT`, and a `detail`
  that was nlohmann's `what()`, which for a parse error quotes the input;
- the three `POST` routes for a body without `bucket.id`: 400 `detail` `bucket.id is required`;
- `GET /accumulatedBalance` without `partyAccount.id`: 400 `detail` `partyAccount.id query
  parameter is required`;
- `not_found` on the four `GET /{id}` routes: 404 `detail` `No <Resource> <id>`.

There was no ad-hoc `{"error": ...}` body in the service.

**Decision:** The handler errors answer the TMF `Error` through libs/tmf-core, with the user's
ADR-0612 choices (Q2 code = HTTP status as a string, Q3 reason = HTTP reason phrase and message =
the detail text, no `referenceError`, Q6 `application/json;charset=utf-8`). The statuses do not
change.

- `bss/balance-management/src/balance_errors.hpp` (new, header-only) holds `guarded` and
  `not_found`, moved out of `main.cpp`, and `bucket_id_required()` and
  `party_account_id_required()`. Each answers `tmf_core::error_response(status, <the text the
  service already sent as detail>)`. It is not named `tmf_errors.hpp` because `conformance_tests`
  also has `bss/roaming-interconnect/src` on its include path, where ADR-0616 adds a
  `tmf_errors.hpp`.
- The three `POST` routes read their body with `tmf_core::parse_json_body<bss_sid::TopupBalance>`,
  `<AdjustBalance>` and `<ReserveBalance>`. A bad body is a 400 TMF Error with no cause and a
  fixed message that does not echo the input.
- `balance-management` links `tmf_core`. `main.cpp` no longer includes `sbi_core/json_body.hpp`.
- Tests: `tests/conformance/test_balance_management_tmf_error.cpp`, 10
  `BalanceManagementTmfError.*` cases in `conformance_tests` (appended at the end of its source
  line). They drive `guarded`, `not_found` (all four resources), `bucket_id_required`,
  `party_account_id_required` and `parse_json_body` with the three POST DTOs, and check the body
  against the Error definition read from the vendored swagger at test time: required members
  present, every member declared, string members strings, `code` and `status` equal to the status
  digits, the exact content type, and no `cause`, `title`, `detail` or `invalidParams`. They also
  check that `guarded` passes a success through, that it leaves any other exception to the
  framework, and that a malformed body is not echoed back.

**Rejected alternatives.**

- **Keep the handlers inside `main()` and test only through a running server.** That needs the
  service's certificates and a PostgreSQL. A header the routes and the conformance test both
  include tests the code the routes actually run (ADR-0616's shape).
- **A TMF654-specific `code` vocabulary.** TMF654 defines no code list either. Q2 applies.
- **Map the store's "partyAccount ... does not exist" rejection to 404 or 409.** This PR keeps the
  statuses; `POST` declares no 404 anyway. Changing one is a separate decision.
- **Read the Error definition from the TMF654 swagger in the test.** That file is vendored by
  ADR-0611 on a branch this PR is not cut from. The test reads TMF640's, which is identical. It can
  switch to TMF654 once ADR-0611 is merged.
- **Name the header `tmf_errors.hpp` like ADR-0616.** Two same-named headers on one target's
  include path is a latent wrong-file include; the test includes this one by relative path anyway.

**Disclosed.**

- The sbi-core framework's own errors on this server are still ProblemDetails: 404 for no route
  (this includes the TMF654 operations the service does not route: the three list `GET`s on
  `topupBalance`/`adjustBalance`/`reserveBalance`, `GET /accumulatedBalance/{id}`, TransferBalance
  and BalanceActionHistory), 405, 501, 413, the duplicate-member 400 (ADR-0354), the TPS ceiling's
  and the pool-exhaustion 503s, and the 500 for an uncaught exception, which also carries the 3GPP
  cause `SYSTEM_FAILURE`. That is BSS SBI-03 PR 7 (ADR-0612 Q4). TMF654 declares no 413, 501 or
  503.
- `tmf_core::parse_json_body`'s own comment says it is for a generated `tmf_gen` DTO. Here T is
  the hand-written `bss_sid` TMF654 DTO (libs/bss-sid/include/bss_sid/balance.hpp). The template
  works for any `from_json` type; the mismatch is the comment's, not the behaviour's.
- An id-less `bucket` object now fails inside the parse (`BucketRef` reads `id` with `at()`), with
  the fixed message `The request body lacks a mandatory member`, before the route's own
  `bucket.id is required` check. That was already the case before this PR (as a ProblemDetails
  `MANDATORY_IE_MISSING`); only `"bucket":{"id":""}` or no `bucket` reaches the route's check.
- The store's `InvalidRequest` texts are forwarded as `message` as before. The unknown-account text
  carries the account id the caller sent; the date-time text carries a field name.
- The 404 message carries the requested id, as the old `detail` did.
- The 401 and 403 that TMF654 declares are never sent: the service has no OAuth2, only mTLS (its
  header's disclosed simplification). 409 is never sent either.
- The success responses (`201`, `200`) still carry `application/json`, not the swagger's
  `application/json;charset=utf-8`. Out of this PR's error-only scope.
- No repo consumer reads the old error bodies: CHF, the integration tests, tools/mcp-server and
  tools/cdr-traffic-gen only check statuses or call the store directly (grep, 2026-11-22). An
  external client that parsed `detail` or `cause` would break.
- `balance_errors.hpp` cites TMF654 swagger line numbers from ADR-0611's branch. Until that is
  merged, the file is not in this branch.
- The tests exercise `balance_errors.hpp` and `tmf_core::parse_json_body`, not the route lambdas in
  `main.cpp`, which need certs and a database to run. The lambdas are covered by the syntax-only
  compile and by review.

**Verified.** Measured on 2026-11-22. `clang-format-18 --dry-run -Werror` passes on the three
changed C++ files. A syntax-only compile of `src/main.cpp` with the flags of its
`compile_commands.json` entry, plus libs/tmf-core's include directory, passes (one pre-existing
`-Wmissing-field-initializers` warning on `TlsConfig`, a line this PR does not touch). The new
test file and libs/tmf-core's `src/error.cpp` were compiled from this branch and linked by hand
into a standalone binary with the main checkout's prebuilt `balance_management_store`, `bss_sid`,
`sbi_core` and `tmf_generated` archives (the sources of those libraries, and specs/tmforum, are
the same in this branch). 10 of 10 tests passed. With `balance_errors.hpp` put back to
ProblemDetails bodies, the same binary failed the four helper cases
(`StoreRejectionIs400TmfErrorWithStoreMessage`, `UnknownResourceIs404TmfError`,
`MissingBucketIdIs400TmfError`, `MissingPartyAccountFilterIs400TmfError`). The `balance-management`
and `conformance_tests` CMake targets were not built, no `ctest` was run, and the live service
was not driven over HTTP.
