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
| 2 | NRF token issuance: requester registered, `nfInstanceId` bound to the client certificate, NF type allowed the requested service (TS 33.501 13.4.1.1.2). | Done, switch off in `config/nrf.json`: `nfs/nrf/src/token_authz.cpp`. Unregistered requester, `nfType` differing from its profile, or a client certificate not naming its NF type -> 400 `invalid_client`; a service no registered producer of `targetNfType` offers to the consumer (`allowedNfTypes`/`Domains`/`Plmns`/`Nssais`, service- then profile-level, `nfServiceList` or deprecated `nfServices`) -> 400 `invalid_scope`; errors are now `AccessTokenErr` with `Cache-Control: no-store` (TS 29.510 Table 6.3.6.3-1, RFC 6749 5.2). Config `access_token_authorization` (env `NRF_ACCESS_TOKEN_AUTHORIZATION`), default true. Tests: `NrfTokenAuthz.*` (8); with the env on, `HelloNfIntegration` is refused, as expected (below) |
| 3 | Consumers: one sbi-core token provider cached per (target NF type, service); every outbound SBI call in all NFs carries a token for its target service. | Done: `sbi_core::TokenSource` picks the scope and `targetNfType` of a request's API from `api_catalog` (generated from the YAMLs, the producer NF type from the file name) and caches one `OAuth2Client` per pair; `http2::Client` asks it for every request sent without an `Authorization` header. `install_default_token_source` makes it the process-wide default; each NF installs it in `run_nrf_lifecycle` (23 NFs), the SCP and SEPP attach it to their NRF client only (a relay must not put its own token on forwarded requests). NFRegister now goes without a token (hello-nf fetches its token after registering). Tests: `Http2Authorizer.*` (3), `SbiApiCatalog.*` (1). Verified with the NRF check on: a live PCF's pre-registration token request is refused (`invalid_client`, not registered), its NFRegister is accepted without a token (201) and its heartbeat 15 s later carries one. `HelloNfIntegration` still fails with the check on: hello-nf registers as an AMF with the `hello-nf` certificate, which the phase-2 binding rightly refuses (phase 5) |
| 4 | `oauth2_required: true` NF by NF as its consumers are migrated, each verified end to end; ends with every NF enforcing. | In progress, 4 NFs enforcing (below, "Phase 4 status") |
| 5 | Tests: helpers fetch tokens; negative tests (no token 401, wrong `aud` 401, missing scope 403, notification without token accepted). Then SEC-03 (AAnF: register/remove only from an AUSF token; `afId` bound to the caller). | Not started |

**Required scope per API.** The service-level scope is read from each API's YAML, not assumed to be
its apiName: `tools/sbi-codegen/gen_api_catalog.py` writes `libs/sbi-core/src/api_catalog.inc`
(142 APIs). Four differ from their apiName, e.g. `nnef-smservice` -> `nnef-smcontext`
(TS29541_Nnef_SMService.yaml), `3gpp-akma` -> `nnef-akma:gpsi-access` (TS29522_AKMA.yaml). An
apiName not in the table (a project-owned API, e.g. the UDR OAM API) requires the apiName itself.
The table is checked in; nothing yet fails CI when the YAMLs change and it is not regenerated.

**Phase 4 status.** Each enforcing NF reads `oauth2_required` from `config/<nf>.json` (env
`<NF>_OAUTH2_REQUIRED`), default true, shown in the GUI schema. Each has a test driving real nrf +
NF processes through `tests/integration/oauth2_enforcement.hpp`: no token 401 with a bare Bearer
challenge, another NF type's token 401 `invalid_token`, another service's token 403
`insufficient_scope`, its own token reaches the handler. Each test also fails with the NF's switch
off. Every API below lists 401/403 and admits tokenless requests in its YAML (`security: - {}`);
requiring a token is the local configuration TS 29.500 6.7.3 allows.

| NF | APIs enforced | Exempt | Consumers in this repo | Test |
|---|---|---|---|---|
| 5G-EIR | n5g-eir-eic | -- | none | `EirAuthorization.*` |
| GMLC | ngmlc-loc | -- | none | `GmlcAuthorization.*` |
| LMF | nlmf-loc, nlmf-dataexposure | nlmf-broadcast: TS29572_Nlmf_Broadcast.yaml declares no security scheme, so there is no scope to require; mTLS only | none | `LmfAuthorization.*` |
| NSSF | nnssf-nsselection, nnssf-nssaiavailability | -- | none | `NssfAuthorization.*` |

None of these NFs has a Helm chart (pre-existing; their compose entries mount `config/`).

**Phase 2 findings.**
- **NFs fetch a token before registering.** With the NRF check on, hello-nf is refused
  (`invalid_client`, not registered): it requests its token before its NFRegister, while TS 33.501
  13.4.1.1.2 makes registration a pre-requisite. Phase 3 must order registration first in every NF;
  the switch stays off until then.
- **Certificate binding is to the NF type, not the instance.** The lab certificates carry
  `CN=<nf>`/`DNS:<nf>` (`scripts/gen-lab-pki.sh`), so the NRF checks that the certificate names the
  consumer's registered NF type (TS 29.510 5.4.2.2.1 allows checking attributes "e.g. NF type"
  against the certificate). A holder of one NF type's certificate can still claim another instance
  of the same type.
- **Test clients request tokens as an unregistered `test-client` with the `hello-nf` certificate**
  (66 places). Phase 5 replaces them with registered identities.
- **Discovery reads only the deprecated `nfServices`.** `profile_discoverable_by`
  (`nfs/nrf/src/discovery_authz.cpp`) ignores `nfServiceList` (TS29510_Nnrf_NFManagement.yaml
  NFProfile); the new token check reads both. Not fixed here.
- An omitted `targetNfType` still defaults to NRF, and `targetNfInstanceId` (TS 33.501 13.4.1.1.2
  1b) is ignored, as TS 29.510 5.4.2.2.1 permits for unsupported parameters: the token's `aud` is
  the NF type.

**Phase 3 findings.**
- **A refused or failed token fetch is not retried for 5 s,** and the request goes without a token;
  the producer then decides. So the first NFRegister of a cold start is tokenless (as allowed), and
  a heartbeat 15 s later gets the token.
- **A 401 `invalid_token` is not acted on:** a token the producer rejects (e.g. the NRF restarted
  with a new signing key) stays cached until it expires.
- **Not every API names its producer.** 6 of 142 catalog rows have no NF type, so requests to them
  go without a token: the file name names no NF (`TS29522_AKMA.yaml`, `TS29522_VFLInference.yaml`,
  `TS24558_Eees_AppContextRelocation.yaml`), names none in the NFType enum (`TS29309_Nbsp_GBA.yaml`),
  or matches one only by prefix (`Niwmsc` / `Nrouter` vs `SMS_IWMSC` / `SMS_ROUTER`, not guessed).
  Names that equal an enum value once `_` is removed are matched (`Nmbsmf` -> `MB_SMF`, also
  `IMS_AS`, `IP_SM_GW`, `SOR_AF`). Project-owned APIs (e.g. `udr-oam`) are not in the catalog.
- **A request to a callback URI whose first path segment is a catalog apiName gets a token** for
  that API; harmless (the notification route ignores it) but a needless token request.
- The 62 explicit `OAuth2Client`s stay; their header wins over the default.

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
