## ADR-0619: bss/product-catalog answers the TMF620 Error via libs/tmf-core (BSS SBI-03 PR 5)

**Date:** 2026-11-22. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SBI-03 for the BSS, PR 5 of the series ADR-0612 describes: one
service moves onto the shared TM Forum `Error` helper. `bss/product-catalog` serves one API, TM
Forum TMF620 Product Catalog Management v4.1.0, at `/tmf-api/productCatalogManagement/v4` (its
`src/main.cpp` header). It has twelve routes: `POST`, `GET` (list), `GET /{id}` and
`DELETE /{id}` on each of `productOffering`, `productOfferingPrice` and `productSpecification`.
It serves no project-internal API, so ADR-0612's Q5 has nothing to apply to here. Its request
DTOs are the hand-mapped `bss_sid` types (`bss_sid/product.hpp`), not `tmf_gen` ones; this ADR
does not change that.

In `specs/tmforum/TMF620-ProductCatalog-v4.1.0.swagger.json` (vendored by ADR-0611, commit
`24e69f30` of tmforum-apis/TMF620_ProductCatalog), every 4xx/5xx response of each of these twelve
operations has the schema `#/definitions/Error` (lines 9165-9208). Compared as parsed JSON, that
definition is identical to TMF640's, which libs/tmf-core builds. `produces` is
`application/json;charset=utf-8`. Each `POST` declares 400, 401, 403, 405, 409 and 500 (no 404).
Each list `GET`, `GET /{id}` and `DELETE /{id}` declares 400, 401, 403, 404, 405, 409 and 500.
So every status this service's handlers send (400, 404, 409) is declared where it is sent.

Before this ADR, the service's handlers answered these errors, all as 3GPP ProblemDetails
(`application/problem+json`):

- the `guarded` wrapper (ADR-0384): a store `InvalidRequest` gave 400 `{"status":400,"title":
  "Bad Request","detail":<store text>}`, a store `Conflict` gave 409 with title `Conflict`;
- `sbi_core::http2::parse_json_body` on the three `POST`s: a bad body gave 400 with the 3GPP
  cause `INVALID_MSG_FORMAT`, `MANDATORY_IE_MISSING` or `MANDATORY_IE_INCORRECT`, and a `detail`
  that was nlohmann's `what()`, which for a parse error quotes the input;
- `GET /{id}` and `DELETE /{id}` for an unknown id, six sites: 404 `{"status":404,"title":
  "Not Found","detail":"No <Entity> <id>"}`.

There was no ad-hoc `{"error": ...}` body in the service.

**Decision:** The handler errors answer the TMF `Error` through libs/tmf-core, with the user's
ADR-0612 choices (Q2 code = HTTP status as a string, Q3 reason = HTTP reason phrase and message =
the detail text, no referenceError, Q6 `application/json;charset=utf-8`). The statuses do not
change.

- `bss/product-catalog/src/tmf_errors.hpp` (new, header-only) holds `guarded`, moved out of
  `main.cpp`, and `not_found(entity, id)`. `guarded` now answers
  `tmf_core::error_response(400, e.what())` for `InvalidRequest` and
  `tmf_core::error_response(409, e.what())` for `Conflict`. `not_found` answers
  `tmf_core::error_response(404, "No " + entity + " " + id)`. The message texts are the ones the
  service already sent as `detail`.
- The three `POST`s read their bodies with `tmf_core::parse_json_body<bss_sid::...>`. A bad body
  is a 400 TMF Error with no cause and a fixed message that does not echo the input.
- `product-catalog` links `tmf_core`. `main.cpp` no longer includes `sbi_core/json_body.hpp`.
- Tests: `tests/conformance/test_product_catalog_tmf_error.cpp`, 8 `ProductCatalogTmfError.*`
  cases in a new `test_product_catalog_tmf_error` binary (product_catalog_store + tmf_core +
  gtest, no database). They drive `guarded`, `not_found` and `parse_json_body` on the three DTOs
  and check the body against the Error definition read from the vendored swagger at test time:
  required members present, every member declared, string members strings, `code` and `status`
  equal to the status digits, the exact content type, and no `cause`, `title`, `detail` or
  `invalidParams`. They also check that `guarded` passes a success through, that it leaves any
  other exception to the framework, and that a malformed body is not echoed back.

**Rejected alternatives.**

- **Keep the handlers inside `main()` and test only through a running server.** The existing
  integration tests (`test_product_catalog_postgres.cpp`, `test_product_catalog_lossless.cpp`)
  drive the stores, not HTTP; an HTTP-level test would need the service's certificates and a
  PostgreSQL. A header the routes and the conformance test both include tests the code the routes
  actually run (the ADR-0616 shape).
- **Add the test to `conformance_tests`.** That target does not link `product_catalog_store` or
  include `bss/product-catalog/src`, and its source and link lines are touched by every parallel
  SBI-03 PR. A separate binary, like `test_tmf640_service`, keeps the link small and avoids the
  conflict.
- **A TMF620-specific `code` vocabulary.** TMF620 defines no code list. Q2 applies.
- **Move the request DTOs to generated `tmf_gen` TMF620 types in this PR.** Out of scope: this PR
  changes only the error bodies.
- **Read the Error definition from the TMF620 swagger in the test.** That file is vendored by
  ADR-0611 on a branch this PR is not cut from. The test reads TMF640's, identical as parsed JSON
  (checked for this ADR). It can switch to TMF620 once ADR-0611 is merged.

**Disclosed.**

- The sbi-core framework's own errors on this server are still ProblemDetails: 404 for no route,
  405, 501, 413, the duplicate-member 400 (ADR-0354), the TPS ceiling's and the pool-exhaustion
  503s, and the 500 for an uncaught exception, which also carries the 3GPP cause `SYSTEM_FAILURE`.
  That is BSS SBI-03 PR 7 (ADR-0612 Q4). Of these, TMF620 declares 400, 404, 405 and 500. It
  declares no 413, 501 or 503.
- The store's `InvalidRequest` and `Conflict` texts are forwarded as `message` as before. They are
  fixed strings plus a field name or the referenced id, never request bodies.
- The 404 message carries the requested id, as the old `detail` did.
- The 401 and 403 that TMF620 declares are never sent: the service has no OAuth2, only mTLS (its
  header's disclosed simplification).
- The success responses are unchanged: the three `POST` 201s still say `application/json`, not
  the swagger's `application/json;charset=utf-8`. Only error bodies are in scope here.
- `tmf_errors.hpp` cites TMF620 swagger line numbers from ADR-0611's branch. Until that is merged,
  the file is not in this branch.
- CHF (`nfs/chf/src/charging_engine.cpp`) is the only in-repo HTTP consumer of this API; it checks
  the status only, never the error body, so it is unaffected.

**Verified.** Measured on 2026-11-22. `clang-format-18 --dry-run -Werror` passes on the three
changed C++ files. A syntax-only compile of `src/main.cpp` with the flags of its
`compile_commands.json` entry, plus libs/tmf-core's include directory, passes (one pre-existing
`-Wmissing-field-initializers` warning on `TlsConfig`, a line this PR does not touch). The new test
file and `libs/tmf-core/src/error.cpp` were compiled and linked by hand into a standalone binary,
with the main checkout's prebuilt `bss_sid`, `sbi_core` and `tmf_generated` archives (the sources
of those libraries, and `bss/product-catalog/src/store.*`, are the same in this branch). 8 of 8
tests passed. With `tmf_errors.hpp` put back to the old ProblemDetails bodies, the same binary
failed the 400, 409 and 404 cases (`StoreInvalidRequestIs400TmfErrorWithStoreMessage`,
`StoreConflictIs409TmfErrorWithStoreMessage`, `UnknownEntityIs404TmfErrorForEachResource`). The
three body-parse cases call `tmf_core::parse_json_body` directly, so they check the helper with
this service's DTOs rather than the route's call site. The `product-catalog` and
`test_product_catalog_tmf_error` CMake targets were not built, and no `ctest` was run. The live
service was not driven over HTTP.
