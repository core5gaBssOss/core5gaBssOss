## ADR-0534: WWW-Authenticate on routes that check their own token; spec causes on non-404 errors (SBI-03 increment 1)

**Date:** 2026-10-26. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SBI-03, still open after ADR-0502. At `dc02145`, 540 of 970
`problem_response` calls carry no `cause`: 483 are 404s, whose causes are API-specific (later
increments, one NF at a time); six are 401s answered without `WWW-Authenticate`; the remaining 51
have other statuses. Reading the BSF found two more cause-less 403s that build `ExtProblemDetails`
directly. User direction 2026-10-26: this increment first (the 401s and the non-404 errors), then
the 404s per NF.

**Decision.**
- `sbi_core::http2::invalid_token_response(req, detail)` (`authorizer.hpp`) answers a failed access
  token with the Authorizer's own 401: `WWW-Authenticate: Bearer realm="<API URI>",
  error="invalid_token"` (RFC 6750 3, 3.1) and no cause, since TS 29.500 Table 5.2.7.2-1 defines
  none for an invalid token. The six routes that check a presented token themselves
  (`RouteAuth::None`) use it: NRF `PUT /nnrf-nfm/v1/nf-instances/{nfInstanceID}` and
  `GET /nnrf-disc/v1/nf-instances`, the ADRF model-file GET, LMF `POST .../cipher-key-data`, and the
  MFAF and NWDAF `POST .../fetch`.
- A cause wherever the spec defines one for the condition:
  - BSF, a second PDU-session binding for the same supi + dnn + snssai (TS 29.521 4.2.2.2) and a
    second MBS session binding for the same mbsSessionId (4.2.2.4 step 2): 403 with
    `EXISTING_BINDING_INFO_FOUND` (table 5.7.3-1), which both clauses mandate.
  - AUSF SoR protection while the CounterSoR is about to wrap: 503 `COUNTER_WRAP` (TS 29.509 table
    6.2.7.3-1).
  - ADRF, the 502 when the DCCF or NWDAF refuses a subscription: `INBOUND_SERVER_ERROR` when that
    peer answered 5xx or 429 (TS 29.500 table 5.2.7.2-1 and 6.4.2.1); none for a 4xx or no answer.
  - NEF, a TSCTSF 503 or 429 to an AsSessionWithQoS create: 502 `INBOUND_SERVER_ERROR` instead of
    relaying the overload status (TS 29.500 6.4.2.1; its relay exception covers only a V-SMF/I-SMF).
    TS29122_AsSessionWithQoS.yaml lists no 502 explicitly; its `default` response and TS 29.122
    table 5.2.6-1 NOTE 1 ("other valid HTTP response codes, if applicable") cover it.

**Not changed: no cause is defined, or the API is not 3GPP.**
- 26 × 501: TS 29.500 5.2.7.2 NOTE 1, no cause is needed for 501. Separately, these 501s answer
  operations this project does not implement, while TS 29.500 defines 501 for a method that no
  resource of the API supports. That deviation predates this ADR and each handler discloses it.
- The sbi-core JSON Patch 415: Table 5.2.7.2-1 has no 415 entry; `Accept-Patch` is already set.
- 12 BSS sites (10 × 400, 2 × 409): TM Forum APIs, not 3GPP SBI.
- UDR OAM provisioning 403 and the CHF `/chf-admin` 504: project-owned APIs.
- BSF, a second PCF-for-a-UE binding (403): a check this project added (the BSF header's
  simplification 5); TS 29.521 4.2.2.3 defines no rejection, so there is no cause to send.
- NEF AAnF unreachable (504), AAnF malformed answer (502), TSCTSF unreachable (503); MFAF message-bus
  publish failure (503); SMSF MT SMS not delivered (504; TS 29.540 table 6.1.7.3-1 has no error for
  it, as the SMSF code already notes): no defined cause fits.
- UDM 503 "UDR unavailable": `AuthDataError::Unavailable` covers a token failure, an unreachable
  UDR, any UDR status other than 200/404, and exhausted CAS retries. Only a UDR 503 or 429 is
  6.4.2.1's 502 `INBOUND_SERVER_ERROR`; splitting the enum is a UDM change for a later increment.

**Rejected alternatives.**
- *Moving the six routes onto the Authorizer (`RouteAuth::OAuth2`).* They are `RouteAuth::None`
  routes that serve requests without a token; requiring one changes who may call them, which is not
  this finding.
- *A cause on the invalid-token 401.* None is defined; `ACCESS_TOKEN_CLAIM_MISSING` is only for
  missing claims.
- *`COUNTER-WRAP`.* TS 29.509 j60 spells it so in 5.3.2.2.1's procedure text, but the
  application-error table, where causes are defined, says `COUNTER_WRAP` (user decision 2026-10-26;
  README "3GPP specification gaps").
- *`TARGET_NF_NOT_REACHABLE` or `TIMED_OUT_REQUEST` for an unreachable peer.* They describe an
  unreachable target NF and a request that timed out at its HTTP client per
  `3gpp-Sbi-Max-Rsp-Time` (6.11.2), not a producer whose own upstream is down.

**Disclosed.**
- The BSF's PDU-session duplicate check runs without the "SamePcf" feature and `paraCom` attribute
  that TS 29.521 4.2.2.2 conditions it on (unchanged; now it carries the clause's cause).
- Not covered by a test: AUSF `COUNTER_WRAP` (it needs 65,535 SoR uses or a seeded counter) and the
  ADRF/NEF `INBOUND_SERVER_ERROR` paths (they need a peer answering 5xx or 429).

**Verified (2026-10-26).**
- TS texts read: TS 29.500 j71, TS 29.509 j60, TS 29.521 j70, TS 29.540 j60
  (`tools/specs/fetch_3gpp_specs.py`; `MANIFEST.tsv` left unchanged).
- `g++` 13.3 and `clang++` 18.1.3, `-std=c++23 -fsyntax-only` with the project warning flags, on
  the 11 changed translation units: no errors; no warning on a changed line beyond the `TlsConfig` initializer
  pattern every integration test's `make_client()` has. `clang-format-18 --dry-run -Werror` clean.
- Tests added, not run here (no build tree in this environment): `BsfExistingBinding.*` (both 403s
  carry `EXISTING_BINDING_INFO_FOUND` and the existing PCF) and
  `SbiAuthorizer.ARouteCheckingItsOwnTokenAnswersTheSameInvalidTokenChallenge`.
