## ADR-0554: Strict NRF token refusal per TS 33.501 13.4.1.1.2; consumer shows the AccessTokenErr value

**Date:** 2026-11-02. **Status:** Closed (work pushed; last citing commit d05994be on origin/main, 2026-11-10). Proposed.

**Context.** The question was what the NRF should do when an NF asks for an access token for a
target NF type (for example UDM or AMF) and no instance of that type is registered. TS 33.501
§13.4.1.1.2 makes a registered, authorised producer a precondition for issuing the token, and
TS 29.510 (Nnrf_AccessToken, Table 6.3.4.2.2-2) says a refusal is returned as 400/401 with an
`AccessTokenErr` body. Its `error` value must be one of the six RFC 6749 §5.2 codes in
`TS29510_Nnrf_AccessToken.yaml` (`invalid_request`, `invalid_client`, `invalid_grant`,
`unauthorized_client`, `unsupported_grant_type`, `invalid_scope`). A ProblemDetails 400 is
allowed only when an SCP or SEPP raises the error, and a 403 ProblemDetails with cause
`MISSING_PARAMETER` only when requester information is missing. The NRF already refuses this
way (`nfs/nrf/src/token_authz.cpp`: 400, `Cache-Control: no-store`, `invalid_scope` when no
producer is registered). The consumer, though, logged only "status N: <raw body>".

**Decision:**
1. Keep strict rejection and add no flag. The NRF issues no token unless a producer of the
   target type is registered and authorised. This was user-directed on 2026-11-02 ("as per 3GPP
   standards").
2. The NRF logs each refusal at `warn` instead of `info`, giving the AccessTokenErr value and
   its description.
3. `sbi_core::describe_token_refusal()` (libs/sbi-core) gives the consumer a readable message:
   - For 400/401 it shows `AccessTokenErr <error>: <error_description> (<error_uri>)`. If the
     `error` value is not one of the six enum values it is flagged as such
     (`kAccessTokenErrCodes`).
   - A 400 ProblemDetails (from an SCP or SEPP) and a 403 ProblemDetails are shown with cause,
     detail and invalidParams.
   - Any other 400/401 body is flagged as a non-conformant refusal.
   - `OAuth2Client::fetch_token()` returns this text in its error.

**Rejected alternatives.**
- A configuration flag that issues tokens for unregistered target types. It breaks the TS 33.501
  §13.4.1.1.2 precondition, and with no producer to name in the scope check, the audience of
  such a token would be unchecked.
- Issuing the token and leaving the producer to refuse it later. That moves the authorisation
  decision away from the NRF, which TS 33.501 makes the authorisation server.
- A ProblemDetails body for these refusals. TS 29.510 keeps ProblemDetails 400 for errors raised
  by an SCP or SEPP.

**Disclosed.**
- The consumer change only affects the error text. Retry and back-off behaviour stay as they were.
- `error_uri` is shown as received and is not dereferenced.
- Verification: the new tests are in `tests/integration/test_nrf_token_authz.cpp`, and their CI
  result is recorded once the PR's run completes. Only syntax-only compile checks were run
  locally, because CI shares this host.
