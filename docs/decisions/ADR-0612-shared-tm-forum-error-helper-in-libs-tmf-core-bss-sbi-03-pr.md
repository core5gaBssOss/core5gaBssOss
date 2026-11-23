## ADR-0612: Shared TM Forum Error helper in libs/tmf-core (BSS SBI-03 PR 1)

**Date:** 2026-11-21. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SBI-03 for the BSS: the BSS servers that expose TM Forum Open APIs
answer errors in mixed shapes. Some send 3GPP ProblemDetails, with 3GPP causes, on a TM Forum API.
Some send ad-hoc `{"error": ...}` bodies. Only the TMF640 handlers in `bss/provisioning`
(`src/main.cpp`, the `tmf_error` lambda) build the TM Forum `Error`. The user approved a series
of PRs, one subsystem each. This is PR 1: a shared helper that no BSS service uses yet. PR 2 and
later move one service each onto it.

The shape comes from `definitions.Error` in
`specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json:3405-3448`. It requires
`[code, reason]` and declares `code`, `reason`, `message`, `status` (all string),
`referenceError` (uri), `@baseType`, `@schemaLocation` and `@type`. The swagger's top-level
`produces` (lines 16-18) is `application/json;charset=utf-8`. `libs/tmf-generated` already
generates the DTO from that file as `tmf_gen::Error` (ADR-0568).

**Decision:** A new static library `libs/tmf-core` (target `tmf_core`, header
`tmf_core/error.hpp`) provides `tmf_core::error_response(int status, std::string message)`,
which returns a `sbi_core::http2::Response` whose body is the generated `tmf_gen::Error`
`{code, reason, message, status}` with content type `application/json;charset=utf-8`. It also
provides `tmf_core::parse_json_body<T>`, which mirrors `sbi_core::http2::parse_json_body` but
answers a 400 TMF Error with no 3GPP cause and never echoes the request body.

- The user's decisions (2026-11-21), recorded here as theirs:
  - **Q2.** `code` is the HTTP status as a string, which is what the TMF640 handlers already do.
    TMF640 defines no code list, so no code is invented.
  - **Q3.** `reason` is the HTTP reason phrase (e.g. "Not Found") and `message` is the detail
    text. `referenceError` is left absent. `@baseType`, `@schemaLocation` and `@type` are left
    absent too.
  - **Q4.** On TMF servers, the sbi-core framework errors (404 no route, 405, 413, 415, auth)
    get an opt-in TMF format later, in PR 7. Not in this PR.
  - **Q5.** The project-internal BSS APIs (`/bss-api/...`, `/provisioning/v1/...`) keep
    ProblemDetails but stop sending 3GPP causes. That is a later PR, not this one.
  - **Q6.** The content type is `application/json;charset=utf-8`, the TMF640 swagger
    `produces`.
  - **Q7.** The helper lives in a new `libs/tmf-core`.
- `status` is the HTTP status as a string, because the swagger types it `string`
  ("HTTP Error code extension").
- `reason` comes from a fixed table: the RFC 9110 section 15.5/15.6 reason phrases for 4xx/5xx,
  plus 429 from RFC 6585 section 4. The BSS uses 400, 404, 409, 500 and 502 today, and the table
  is wider so that PR 2+ and PR 7 (405, 415, 503, ...) need no change here. For a status outside
  the table, `reason` falls back to the status digits. The required member is never empty and
  no phrase is made up.
- `parse_json_body` messages are fixed text ("The request body is not well-formed JSON", "The
  request body lacks a mandatory member", "A member of the request body has an invalid type or
  value"). nlohmann's own `what()` for a parse error quotes the offending input
  (`last read: '...'`), so it is not forwarded.
- The body is serialised with `error_handler_t::replace`, so a message with invalid UTF-8
  cannot turn an error response into an exception.
- Tests: `tests/conformance/test_tmf_error.cpp`, 9 `TmfError.*` cases in `conformance_tests`.
  They read the Error definition and `produces` from the vendored swagger at test time
  (`TMF640_SWAGGER_PATH`). They check that the required members are present, that every member
  is a declared property, that string-typed members are strings, that the content type is exact,
  and that there is no `cause`, `title` or `detail`. They also cover malformed JSON, a missing
  member and a wrong-typed member: each gives a 400 TMF Error with no cause and without echoing
  the input.

**Rejected alternatives.**

- **A project code list for `code`** (e.g. `SERVICE_NOT_FOUND`). TMF640 defines none, so this
  would be an invented vocabulary (CLAUDE.md guardrail 1). The user chose the HTTP status (Q2).
- **The TMF630 API design guidelines' error code list.** TMF630 is not vendored in this repo,
  and its list cannot be cited from a file in hand. Using it from memory would be fabrication.
- **Per-service helpers** (copying the provisioning `tmf_error` lambda into each BSS service).
  That is the duplication SBI-03 is about: five copies would drift in content type and member
  choice.
- **Putting it in `libs/bss-sid`.** bss-sid is deliberately independent of `sbi_core`. Its
  header says so, on ODA component-boundary grounds. An HTTP response helper needs
  `sbi_core::http2::Response`.
- **A hand-written Error struct.** `tmf_gen::Error` is generated from the swagger. CLAUDE.md
  forbids hand-writing a DTO the spec can generate.

**Disclosed.**

- The helper is based on TMF640 only. That TMF620, TMF632, TMF651 and TMF654 declare the
  identical `Error` definition is confirmed by their vendoring in PR 0 (ADR-0611), which is in
  flight in parallel. It is not confirmed by this ADR.
- No BSS service uses the helper yet. Until PR 2+ land, BSS error responses are unchanged.
  `bss/provisioning`'s own `tmf_error` lambda still uses `Response::json`, i.e.
  `application/json` without the `charset` parameter.
- sbi-core framework errors on the TMF servers (Q4, PR 7) and the internal-API cause removal
  (Q5) are not done here.
- `parse_json_body` does not run the duplicate-member check (`json_has_duplicate_keys`, ADR-0354).
  Neither does `sbi_core::http2::parse_json_body`, which it mirrors. A TMF caller that needs the
  check must call it first.
- The reason-phrase table is the RFC registry text, not a TM Forum list. TM Forum leaves `reason`
  free text.

**Verified.** Measured on 2026-11-21 in this branch's build. `cmake --build build --target tmf_core
conformance_tests` finished with 0 failures. `conformance_tests --gtest_filter='TmfError.*'` passed
9 of 9 tests. The full `conformance_tests` binary passed 621 tests in 114 suites. That is this
binary only, not a full `ctest` run.
