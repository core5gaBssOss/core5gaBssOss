## ADR-0499: SBI OAuth2 authorization -- access tokens required by default, phased rollout (REVIEW-5ae0ef1 SBI-01/02, SEC-03)

**Date:** 2026-10-16. **Status:** Accepted (decision by the user, 2026-10-16: "Require tokens by
default"); implementation in progress -- see the phase table.

**Context.** `docs/reviews/REVIEW-5ae0ef1.md` SBI-01: no producer checks the token's `aud`,
`scope` or `sub`, and a request with no token is accepted everywhere (each NF's `check_bearer`
returns "no result" for a missing header; handlers reject only an invalid token). TS 33.501
13.4.1.2 (TS_33.501_j70.txt:4022-4028): the producer shall check `sub` against the consumer's TLS
certificate, `aud` against its own identity or NF type, and `scope` against the requested
operation. TS 29.500 (TS_29.500_j70.txt:3073) lets a producer accept tokenless requests "based on
local configuration"; that choice is now made: required. SBI-02: 401s carry no `WWW-Authenticate`.

Found while planning (not in the review):
- **The NRF issues any token to anyone.** `/oauth2/token` signs whatever `scope`/`targetNfType`
  is asked, without checking that the requester is registered, that its `nfInstanceId` matches its
  client certificate, or that its NF type may use the service (TS 33.501 13.4.1.1.2). Producer-side
  checks are only as strong as this.
- **Most inter-NF calls send no token.** 62 `OAuth2Client`s exist; e.g. the NEF has none for the
  PCF/UDM/UDR, the AMF none for the UDM, the SMF and TSCTSF none for the UDM. Enforcing first would
  break those flows.

**Decision.** Access tokens are required by default on every SBI producer, reached in phases so no
flow breaks in between:

| Phase | Content | Status |
|---|---|---|
| 1 | sbi-core server-side authorizer, before any handler: no token -> 401 + `WWW-Authenticate: Bearer` (`realm`, `error="invalid_token"` when one was sent); `aud` not own NF type/instance -> 401 `invalid_token`; `scope` lacking the API's service name -> 403 `insufficient_scope`. Exempt: notification/callback routes (TS 33.501 13.4.1.0: "The OAuth 2.0 framework does not apply to the notification operation"), NRF `/oauth2/token`, NRF bootstrapping (no security scheme in its YAML). Per-NF `oauth2_required`. Unit tests. | Done (sbi-core only, no NF switched on): `sbi_core/authorizer.hpp`, `Server::set_authorizer`, `RouteAuth` per route (default `OAuth2`). Also: a token missing a required AccessTokenClaims claim -> 401 `ACCESS_TOKEN_CLAIM_MISSING` with `invalidParams` (TS 29.500 Table 5.2.7.2-1). Tests: `SbiAuthorizer.*` (10), `Http2Authorizer.*` (1) |
| 2 | NRF token issuance: requester registered, `nfInstanceId` bound to the client certificate, NF type allowed the requested service (TS 33.501 13.4.1.1.2). | Not started |
| 3 | Consumers: one sbi-core token provider cached per (target NF type, service); every outbound SBI call in all NFs carries a token for its target service. | Not started |
| 4 | `oauth2_required: true` NF by NF as its consumers are migrated, each verified end to end; ends with every NF enforcing. | Not started |
| 5 | Tests: helpers fetch tokens; negative tests (no token 401, wrong `aud` 401, missing scope 403, notification without token accepted). Then SEC-03 (AAnF: register/remove only from an AUSF token; `afId` bound to the caller). | Not started |

**Required scope per API.** The service-level scope is read from each API's YAML, not assumed to be
its apiName: `tools/sbi-codegen/gen_api_scopes.py` writes `libs/sbi-core/src/api_scopes.inc`
(142 APIs). Four differ from their apiName, e.g. `nnef-smservice` -> `nnef-smcontext`
(TS29541_Nnef_SMService.yaml), `3gpp-akma` -> `nnef-akma:gpsi-access` (TS29522_AKMA.yaml). An
apiName not in the table (a project-owned API, e.g. the UDR OAM API) requires the apiName itself.
The table is checked in; nothing yet fails CI when the YAMLs change and it is not regenerated.

**Disclosed.**
- **`sub` cannot be checked with the current lab PKI.** TS 33.501 matches `sub` (NF instance ID)
  to the NF instance ID in the client certificate's subjectAltName; this repo's NFs generate their
  instance ID at startup and the lab certificates (`scripts/gen-lab-pki.sh`) do not carry one. Needs
  fixed instance IDs and certificates carrying them; open until then.
- Until phase 4 completes, NFs not yet switched still accept tokenless requests (today's behaviour).
- Only the service-level scope is enforced; additional (resource/operation) scopes are not.

**Rejected alternatives.**
- *Opt-in per NF, default off* -- the user chose required by default.
- *Editing each NF's `check_bearer` and its ~957 call sites* -- 25 copies of the same logic; one
  authorizer in sbi-core is enforced the same way everywhere and cannot be forgotten on a new route.
- *Switching enforcement on before consumers send tokens* -- breaks most inter-NF flows at once.
