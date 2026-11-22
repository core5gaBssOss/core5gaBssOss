## ADR-0615: bss/provisioning adopts the shared TM Forum Error helper (BSS SBI-03 PR 2)

**Date:** 2026-11-22. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SBI-03 for the BSS, PR 2 of the series the user approved on
2026-11-21 (ADR-0612). ADR-0612 added `libs/tmf-core` (`tmf_core::error_response`,
`tmf_core::parse_json_body`); no service used it yet. `bss/provisioning` serves two APIs:

- TMF640 Service Activation and Configuration v4 (`/tmf-api/ServiceActivationAndConfiguration/v4`,
  ADR-0558, ADR-0569, ADR-0576, ADR-0585, ADR-0586). Its errors came from a local `tmf_error`
  lambda in `src/main.cpp` that built `tmf_gen::Error {code, reason, message, status}` with
  `Response::json`, so the content type was `application/json` without `charset`, and `reason`
  was whatever title string the call site passed.
- The project-internal order API `/provisioning/v1/customerOrder` (ADR-0382). Its POST errors were
  already `ProblemDetails` without a `cause`. Its GET 404 was an ad-hoc `{"error": "order not
  found"}` body.

The Error shape is `definitions.Error` in
`specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json:3405-3448` (required `[code,
reason]`), as cited by ADR-0612.

**Decision:** `bss/provisioning` builds every TMF640 error with `tmf_core::error_response`, and
the internal order API answers every error as ProblemDetails with no 3GPP cause.

- `provisioning_store` links `tmf_core` (PUBLIC). The `provisioning` executable gets it through
  `provisioning_store`.
- New `provisioning::tmf640::error_response(const ServiceError&)` in `src/tmf640_service.cpp`
  returns `tmf_core::error_response(e.status, e.detail)`. The five call sites that turned a
  `ServiceError` into a response (POST /hub, POST /service twice, PATCH /service/{id}, DELETE
  /service/{id}) call it. `ServiceError::title` is no longer sent: `reason` is the RFC 9110
  phrase for the status (ADR-0612 Q3). Every title in use today (`Bad Request`, `Not Found`,
  `Conflict`, `Internal Server Error`) is already that phrase, so `reason` does not change on the
  wire.
- The `tmf_error` lambda in `src/main.cpp` now takes `(status, message)` and forwards to
  `tmf_core::error_response`. Its six literal call sites (DELETE /hub/{id} 404, GET /service 400
  `fields`, GET /service 400 offset/limit, GET /service/{id} 400 `fields`, GET /service/{id} 404,
  PATCH 400 content type) drop their reason argument.
- On the wire, the TMF640 errors change in one way: the content type becomes
  `application/json;charset=utf-8` (ADR-0612 Q6). Statuses, `code`, `reason`, `message` and
  `status` are unchanged.
- Internal API (ADR-0612 Q5): new `src/order_api_errors.{hpp,cpp}` with `malformed_body()` (400),
  `order_rejected(detail)` (400) and `order_not_found()` (404). All three call
  `sbi_core::http2::problem_response(..., std::nullopt)`, so they send ProblemDetails
  `{status, title, detail}` as `application/problem+json` with no `cause`. The two POST errors are
  byte-for-byte what they were. The GET 404 changes from `{"error": "order not found"}`
  (`application/json`) to ProblemDetails `{status: 404, title: "Not Found", detail: "order not
  found"}`.
- No status changes.
- Tests: `tests/conformance/test_provisioning_errors.cpp`, 6 cases in the `test_tmf640_service`
  binary. `ProvisioningTmf640Error.*` reads the TMF640 `Error` definition from the vendored swagger
  at test time (`TMF640_SWAGGER_PATH`). It checks the required members, that every member is
  declared, the exact content type, code/reason/message/status for 400, 404, 409 and 500, that
  `reason` comes from the status rather than the title, and no `cause`/`title`/`detail`. It also
  checks a refused POST /service body and a refused listener. `ProvisioningOrderApiError.*` checks
  the three internal-API errors: ProblemDetails, `application/problem+json`, no `cause`, no TMF
  member and no `error` member.

**Rejected alternatives.**

- **Routing the TMF640 request bodies through `tmf_core::parse_json_body<T>`.**
  `validate_create`, `plan_patch` and `validate_listener` parse the raw JSON themselves. They must
  first see the raw object: a non-object body, the immutable `id`/`href` on a merge-patch, and the
  RFC 7396 merge before the DTO read. A `parse_json_body<tmf_gen::Service_Create>` up front would
  change which 400 message a client gets and would parse twice. Their 400s already go out as the
  TMF Error, and their fixed "request body is not valid JSON" text never quotes the input.
- **Keeping the `title` argument and passing it as `reason`.** ADR-0612 Q3 makes `reason` the HTTP
  reason phrase. Free text there could drift from the status.
- **Dropping `ServiceError::title`.** It is no longer sent, but removing it would touch every
  `ServiceError` construction site in `tmf640_service.cpp` and `tmf640_events.cpp` and their
  tests. That is churn beyond this PR's error-response scope. It is left, and disclosed.
- **Moving the internal order API to the TMF Error.** It is not a TM Forum API. ADR-0612 Q5 keeps
  ProblemDetails there.

**Disclosed.**

- `ServiceError::title` stays in the struct but is ignored when the response is built. A future
  title that differs from the status phrase would not reach the wire.
- `tmf_core::parse_json_body` is linked but not called in `bss/provisioning` (see Rejected
  alternatives).
- The `GET /provisioning/v1/customerOrder/{id}` 404 body changed shape. No repo consumer (GUI, BFF,
  scripts, tests) reads it (grep, 2026-11-22). An external client that parsed `error` would
  break.
- sbi-core framework errors on the TMF640 server (404 no route, 405, 413, 415, auth) are still
  3GPP ProblemDetails. They are ADR-0612 Q4, PR 7, and not in this PR.
- The `/provisioning/v1/customerOrder` 502 for a failed downstream task is a success-shaped body
  (`provisioningTasks`, `error` string), not an error body. It is unchanged.
- `ProvisioningTmf640Error.*` exercises `tmf640::error_response` and the validators, not the
  route lambdas in `main.cpp`. Those need certs and a database to run. The lambdas are covered by
  the syntax-only compile and by review.

**Verified.** Measured on 2026-11-22 against the main checkout's existing build (not a fresh
configure of this branch). I compiled the changed TUs (`bss/provisioning/src/main.cpp`,
`tmf640_service.cpp`, `order_api_errors.cpp`, `libs/tmf-core/src/error.cpp`, both test files) and
linked them by hand into the `test_tmf640_service` binary against the build's other archives.
26 of 26 tests passed, 6 of them the new `Provisioning*` cases. `clang-format-18 --dry-run
-Werror` is clean on every changed C++ file. CMake has not been reconfigured and no full `ctest`
has run.
