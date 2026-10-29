## ADR-0502: ProblemDetails cause required on every SBI error response; per-handler bearer checks the Authorizer made unreachable removed

**Date:** 2026-10-20. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SBI-03: `sbi_core::http2::problem_response(status, title, detail)`
never set `cause`, and `parse_json_body` answered every failure without one, although TS 29.500
5.2.7.2 (`TS_29.500_j70.txt:2044`) requires the `cause` attribute for the errors of Table
5.2.7.2-1. The review counted ~1,930 call sites. 957 of them (964 by the final count) were per-handler
`check_bearer` blocks, `if (auth && !auth->valid) return problem_response(401, ...)`, written
before ADR-0499. Since ADR-0499 every NF installs the sbi-core Authorizer unconditionally (also
with `oauth2_required: false`, which only admits a request without a token); on every OAuth2 route
it rejects a token failing signature, expiry, issuer, `aud` or scope before the handler runs. The
handler check could therefore never fire on those routes, and it was the source of the 401s
without `WWW-Authenticate` (SBI-02).

**Decision.**

1. **Unreachable handler checks removed (user-approved, 2026-10-19).** 964 blocks on OAuth2
   routes, the 903 `&verifier` lambda captures and 19 `check_bearer` definitions left unused.
   Classified site by site (enclosing `add_route`, its `RouteAuth`, the server's
   `set_authorizer`), not by pattern alone. Kept, because the Authorizer does not run there or the
   handler reads the token: the `RouteAuth::None` routes -- NRF NFRegister/NFUpdate PUT (an
   NFUpdate needs a token, ADR-0499), NWDAF and MFAF `/fetch`, LMF `/cipher-key-data`, ADRF
   model-file GET -- the AAnF `retrieve-applicationkey` (reads `auth->scope`), the ADRF model-file
   helper (reads the subject), and NRF NFDiscover (below). The earlier estimate of 955 left out the
   MTLF routes and the shared helper lambdas (MTLF/NWDAF `validate`, DCCF `unsubscribe`), all
   reached only from OAuth2 routes.
2. **`cause` is a required parameter:** `problem_response(status, title, detail,
   std::optional<std::string> cause)`. Every call states its cause; `std::nullopt` is an explicit,
   greppable "no Table 5.2.7.2-1 cause applies here". Values come only from Table 5.2.7.2-1 (which
   TS 29.122 line 2412 also makes applicable to the northbound APIs).
3. **`parse_json_body` / `parse_multipart_json_body`:** malformed JSON or multipart ->
   `INVALID_MSG_FORMAT`; a DTO read failure -> `dto_error_cause(e)`: `MANDATORY_IE_MISSING` for an
   absent member (generated `from_json` reads mandatory members with `j.at()`, which throws
   `out_of_range`), `MANDATORY_IE_INCORRECT` otherwise.
4. **Assignment (986 calls):**

   | Cause | Calls | Rule |
   |---|---|---|
   | `std::nullopt` | 546 | 488 404s (no table cause: "resource not found" causes are API-specific), 26 501, 1 415, 6 401 (kept checks), 5xx gateway/unavailable, 403, 409, all of `bss/` (TM Forum APIs, not TS 29.500) |
   | `INVALID_MSG_FORMAT` | 125 | malformed JSON / JSON Patch / multipart / unsupported body type |
   | `SUBSCRIPTION_NOT_FOUND` | 121 | 404 on PATCH/PUT/DELETE of a subscription resource whose error text names a subscription |
   | `SYSTEM_FAILURE` | 74 | every 500 |
   | `UNSPECIFIED_MSG_FAILURE` | 41 | other 400s (NOTE 2: no other value fits) |
   | `MANDATORY_IE_MISSING` / `_INCORRECT` | 23 / 22 (+16 `dto_error_cause`) | IE missing / wrong; DTO read failures |
   | `MANDATORY_QUERY_PARAM_MISSING` / `_INCORRECT` | 16 / 1 | required query parameter absent / malformed |

5. **NRF bootstrapping** (`GET /bootstrapping`) reported `oauth2Required: false` for nnrf-nfm and
   nnrf-disc, with a stale comment saying the NRF did not require OAuth2. It now reports the NRF's
   `oauth2_required` (TS29510_Nnrf_Bootstrapping.yaml: "whether the NRF requires Oauth2-based
   authorization for accessing its services"); nnrf-oauth2 stays false.

**Disclosed, not conformant or not done.**
- 488 404s carry no cause. Their causes are per-API application errors (e.g. `USER_NOT_FOUND`,
  `CONTEXT_NOT_FOUND`) defined in each TS 29.5xx stage-3 text, not in the YAML; mapping them needs
  those texts operation by operation -- a follow-up, one NF per increment.
- NOTE 1 of Table 5.2.7.2-1 (`invalidParams` for missing/incorrect IEs and query parameters) is
  not implemented.
- The causes of the 400s outside the parse helpers were assigned from each site's title/detail
  text by rule (table above), then the 71 sites the rules could not decide were reviewed by hand;
  the rule-assigned ones were not each re-read.
- The 6 kept 401s still lack `WWW-Authenticate` (SBI-02 residue); the Authorizer's challenge
  builder is not exposed to handlers.
- BSF 403 "existing binding" has no cause: TS 29.521's application errors are not in the YAML and
  the TS text is not vendored here.
- NRF `NFS.DiscFailUnauth` (TS 28.552 5.10.3.3): since ADR-0499 the Authorizer rejects a bad
  token before the NFDiscover handler, so neither this counter nor `nfs_disc_req` counts it. The
  handler check that increments them is kept but unreachable; counting needs an Authorizer
  rejection hook. Pre-existing since ADR-0499, not fixed here.

**Rejected alternatives.**
- *A default cause derived from the status code* (e.g. every 404 -> a fixed cause): Table
  5.2.7.2-1 has no generic not-found cause; `RESOURCE_CONTEXT_NOT_FOUND` (first proposed here) is a
  400 for a notification receiver. A default would fabricate causes.
- *Keeping the 3-argument overload*: leaves new call sites free to omit the cause again.
- *Keeping the unreachable handler checks and adding causes to them*: 964 dead branches churned.
- *Deleting every `check_bearer` by pattern*: would have removed the checks on `RouteAuth::None`
  routes and the AAnF/ADRF checks that read the token.
