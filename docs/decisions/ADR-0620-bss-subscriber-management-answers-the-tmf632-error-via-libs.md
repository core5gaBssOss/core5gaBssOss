## ADR-0620: bss/subscriber-management answers the TMF632 Error via libs/tmf-core (BSS SBI-03 PR 6)

**Date:** 2026-11-22. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SBI-03 for the BSS, PR 6 of the series the user approved on
2026-11-21 (ADR-0612). ADR-0612 added `libs/tmf-core` (`tmf_core::error_response`,
`tmf_core::parse_json_body`). `bss/subscriber-management` (`src/main.cpp`) serves two APIs, and
every route was wrapped in one `guarded` template:

- TMF632 Party Management v4 (`/tmf-api/party/v4`): POST/GET `/individual`, GET
  `/individual/{id}`, POST/GET `/organization`, GET `/organization/{id}`. The bodies are the
  hand-written `bss_sid::Individual` / `bss_sid::Organization` (libs/bss-sid, ADR-0060), not
  generated DTOs. No other TM Forum API is served.
- The project-internal Account/Subscriber API (`/bss-api/subscriberManagement/v1`): POST/GET
  `/account`, GET `/account/{id}`, POST/GET `/subscriber`, GET `/subscriber/{id}`, GET
  `/subscriber/by-supi/{supi}`.

Before this ADR both APIs answered 3GPP ProblemDetails: `guarded` mapped the store's
`InvalidRequest` to 400 and `Conflict` to 409 (ADR-0386), the five GET-by-id 404s were
`problem_response(404, "Not Found", ...)`, all without a cause. The four POST bodies went through
`sbi_core::http2::parse_json_body`, which sends a TS 29.500 cause (`INVALID_MSG_FORMAT`,
`MANDATORY_IE_MISSING`, `MANDATORY_IE_INCORRECT`) and, for a parse error, nlohmann's `what()`,
which quotes the input.

The TMF632 Error shape is `definitions.Error` in `specs/tmforum/TMF632-Party-v4.0.0.swagger.json`
lines 3919-3962 (on `origin/docs/vendor-tmf-swaggers`, ADR-0611; checked on 2026-11-22: identical
as parsed JSON to TMF640's, required `[code, reason]`, `produces` `application/json;charset=utf-8`).
Every 4xx/5xx of every TMF632 operation references it. POST `/individual` and POST
`/organization` declare 400, 401, 403, 405, 409, 500. GET `/individual/{id}` and GET
`/organization/{id}` also declare 404. Every status this service answers (400, 404, 409) is
declared on the operation that answers it.

**Decision:** The TMF632 routes answer every handler error as the TM Forum `Error` through
libs/tmf-core. The internal routes answer ProblemDetails with no 3GPP cause (ADR-0612 Q5). The user's
ADR-0612 choices apply unchanged: Q2 `code` = the HTTP status as a string, Q3 `reason` = the HTTP
reason phrase and `message` = the detail with no `referenceError`, Q6 content type
`application/json;charset=utf-8`.

- New header-only `bss/subscriber-management/src/api_errors.hpp` (namespace
  `subscriber_management`), kept outside `main()` so tests drive the same code the routes run:
  - `guarded_tmf(handler)`: `InvalidRequest` -> `tmf_core::error_response(400, what)`,
    `Conflict` -> `tmf_core::error_response(409, what)`.
  - `party_not_found(kind, id)`: `tmf_core::error_response(404, "No <kind> <id>")`.
  - `guarded_internal(handler)`: the same 400/409 mapping as ProblemDetails without a cause, byte
    for byte what `guarded` sent.
  - `internal_not_found(detail)`: ProblemDetails 404 without a cause, byte for byte what the
    internal 404s sent.
  - `parse_internal_json_body<T>`: the same titles as `sbi_core::http2::parse_json_body`
    ("Malformed JSON", "Missing or invalid mandatory IE"), no cause. A parse error's detail is the
    fixed text "request body is not valid JSON" (the text bss/provisioning's internal API uses,
    ADR-0615). The detail of a missing or wrongly typed member stays nlohmann's `what()`, which
    names the member and type, not the value.
- `src/main.cpp`: the local `guarded` is removed. The six TMF632 routes use `guarded_tmf`, the two
  TMF632 POSTs use `tmf_core::parse_json_body<bss_sid::Individual / Organization>`, the two TMF632
  404s use `party_not_found`. The seven internal routes use `guarded_internal`, the two internal
  POSTs use `parse_internal_json_body`, the three internal 404s use `internal_not_found`. The
  `sbi_core/json_body.hpp` include is gone.
- `bss/subscriber-management/CMakeLists.txt`: the `subscriber-management` executable links
  `tmf_core`.
- No status changes. The success responses are unchanged.
- On the wire, the TMF632 errors change from ProblemDetails `{status, title, detail[, cause]}`
  (`application/problem+json`) to `{code, reason, message, status}`
  (`application/json;charset=utf-8`). The internal errors change only on the two POSTs: no
  `cause`, and a parse error no longer echoes the input.
- Tests: `tests/conformance/test_subscriber_management_errors.cpp`, 12 cases, added at the end of
  the `conformance_tests` source line, which also links `subscriber_management_store` (for its
  include dir, `store.hpp`'s exceptions and the Account/Subscriber `from_json`).
  `SubscriberManagementTmf632Error.*` (6) reads `definitions.Error` at test time and checks the
  required members, that every member is declared and string-typed, the exact content type,
  `code`/`status` = the status digits, `reason`/`message`, no `cause`/`title`/`detail`/
  `invalidParams`/`referenceError`, for the 400 and 409 store rejections, both 404s, a malformed
  body (not echoed) and a wrongly typed member. `SubscriberManagementInternalError.*` (6) checks
  the internal 400/409/404 and both parse failures as `application/problem+json` ProblemDetails
  with no `cause`, no TMF member and no `error` member, plus a well-formed Subscriber body.

**Rejected alternatives.**

- **One `guarded` with a flag, or one wrapper for both APIs.** Two templates keep each route's
  error shape visible where the route is registered, and a flag would be one more thing to get
  wrong per route.
- **Keeping `sbi_core::http2::parse_json_body` on the internal POSTs.** It sends a TS 29.500 cause,
  which ADR-0612 Q5 removes from the project-internal BSS APIs.
- **Moving the internal Account/Subscriber API to the TMF Error.** It is not a TM Forum API
  (main.cpp header; docs/DATA_MODEL.md). ADR-0612 Q5 keeps ProblemDetails there.
- **Renaming the internal title "Missing or invalid mandatory IE".** It is 3GPP wording, but the
  title is not a cause, and changing it is not in this PR's scope (Q5 removes the cause only). It is
  left, and disclosed.
- **Reading the Error definition from the TMF632 swagger in the test.** That file is vendored by
  ADR-0611 on a branch this one is not cut from. The test reads TMF640's (`TMF640_SWAGGER_PATH`),
  the definition libs/tmf-core is built from, as the ADR-0616 test does for TMF651.

**Disclosed.**

- The TMF632 bodies are still the hand-written `bss_sid` party types, not generated TMF632 DTOs.
  `tmf_core::parse_json_body` reads them; the "never hand-write a DTO" rule for TM Forum shapes is
  not addressed here.
- `bss_sid::Individual` and `bss_sid::Organization` read their members with `find`, so a JSON
  body that is not an object (e.g. `[]`) parses as an empty party rather than a 400. That is the
  existing `from_json` behaviour, unchanged here and not tested as an error.
- The test reads the TMF640 Error definition, not the TMF632 one. They were compared as parsed JSON
  on 2026-11-22 and are identical.
- The internal POSTs keep the title "Missing or invalid mandatory IE" (3GPP wording) with no cause.
- No repo consumer (GUI, BFF, scripts, tests) reads this service's error bodies (grep, 2026-11-22).
  An external client that parsed the old ProblemDetails on the TMF632 routes, or `cause` on the
  internal POSTs, would break.
- sbi-core framework errors on this server (404 no route, 405, 501, 413, the duplicate-member 400,
  the TPS ceiling's and pool-exhaustion 503s, an uncaught exception's 500) are still 3GPP
  ProblemDetails on both APIs. They are ADR-0612 Q4, PR 7, and not in this PR.
- The tests exercise `api_errors.hpp` and `tmf_core::parse_json_body`, not the route lambdas in
  `main.cpp`. Those need certs and a database to run. The lambdas are covered by the syntax-only
  compile and by review.

**Verified.** Measured on 2026-11-22 against the main checkout's existing build (not a fresh
configure of this branch). `bss/subscriber-management/src/main.cpp`, the new test and
`libs/tmf-core/src/error.cpp` compile with `-fsyntax-only` under the build's flags plus the
tmf-core include (the only diagnostic is the existing `-Wmissing-field-initializers` on
`TlsConfig`, a line this PR does not touch). The new test, compiled and linked by hand against the
build's `subscriber_management_store`, `sbi_core`, `bss_sid` and `tmf_generated` archives (their
sources match this branch), passes 12 of 12. `clang-format-18 --dry-run -Werror` is clean on every
changed C++ file. CMake has not been reconfigured and no full `ctest` has run.
