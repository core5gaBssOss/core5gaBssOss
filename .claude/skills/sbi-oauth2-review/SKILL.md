---
name: sbi-oauth2-review
description: Review or change SBI OAuth2 (NRF as authorization server, Nnrf_AccessToken /oauth2/token, producer-side token verification, client credentials assertion) against TS 33.501 §13.3-§13.4.1, TS 33.210 §6.3.3 and TS29510_Nnrf_AccessToken.yaml. Use whenever touching token issue, JWT claims, JWS verification or SBI authorization in nfs/nrf or libs/sbi-core.
---

# SBI OAuth2 review (3GPP profile)

Adapted from `configuring-oauth2-authorization-flow` in
[mukul975/Anthropic-Cybersecurity-Skills](https://github.com/mukul975/Anthropic-Cybersecurity-Skills)
at `54a798831d2266a3ca61ce68a7acb80b81160d57` (Copyright 2026 mukul975, Apache-2.0; skill author mahipal). Most of the
upstream skill is about user-facing OAuth and does **not** apply to the SBI; it was rewritten
against 3GPP. **3GPP wins over generic OAuth advice.**

## What does NOT apply on the SBI (do not "fix" these)

| Upstream advice | Why not here | Source |
|---|---|---|
| Authorization Code + PKCE, Device Grant | Only `client_credentials` (RFC 6749 §4.4); there is no user | TS 33.501 §13.4.1.0; YAML `grant_type` enum |
| Refresh tokens / rotation | Not part of the SBI grant; consumers re-request | TS 33.501 §13.4.1.0 (client_credentials only); RFC 6749 §4.4.3 |
| Revocation endpoint (RFC 7009) | "token revocation is not supported" | TS 33.501 §13.4.1.1.2 |
| DPoP sender-constraining | Binding is `sub` ↔ client TLS certificate instead | TS 33.501 §13.4.1.1.2 step 2 |
| Tokens on every request | Not for notifications; optional for Nnrf_NFManagement/NFDiscovery | TS 33.501 §13.4.1.0, §13.3.1.3 |

## Shapes (from `specs/5G_APIs-REL-19/TS29510_Nnrf_AccessToken.yaml` — grep, never Read whole)

- `POST /oauth2/token`, body `AccessTokenReq`; required `grant_type` (`client_credentials` only), `nfInstanceId`, `scope`.
- `AccessTokenRsp` required `access_token`, `token_type` (`Bearer`).
- `AccessTokenClaims` required `iss`, `sub`, `aud`, `scope`, `exp`; optional incl. `iat`, `producerSnssaiList`, `producerNsiList`, `producerNfSetId`, `producerNfServiceSetId`, `consumerPlmnId`.
- `AccessTokenErr.error` enum: `invalid_request`, `invalid_client`, `invalid_grant`, `unauthorized_client`, `unsupported_grant_type`, `invalid_scope`.

## NRF (authorization server) checklist — TS 33.501 §13.4.1.1.2 step 1

- `client_id` = consumer NF Instance Id; roles: NRF = AS, consumer = client, producer = resource server (§13.4.1.1.1).
- Verify the request's NF instance ID, NF type and PLMN ID match the client certificate or the registered profile; otherwise do not process it.
- Check the consumer is authorized for the target NF type/instance and any slice (S-NSSAI / NSI) restriction; do not issue a token otherwise.
- Claims: `iss` = NRF instance ID, `sub` = consumer instance ID, `aud` = producer NF type **or** producer instance ID(s), `scope`, `exp`.
- Errors use the RFC 6749 error response (`AccessTokenErr`), not ProblemDetails.
- Multiple NRFs (§13.4.1.1.3): the consumer asks the NRF it is registered with; the local NRF signs.

## JWS profile — TS 33.210 §6.3.3

- `ES256` shall be supported; `alg: none` shall not; the `jwk` header shall not be supported.
- `kid` shall be supported; if used, the verifier checks `alg` matches the key's algorithm.
- Access token: `x5t#S256` should be supported. Client credentials assertion: `x5u` and `x5c` shall be supported.
- MAC-signed tokens are allowed but not recommended (TS 33.501 §13.4.1.0).

## Producer (resource server) checklist — TS 33.501 §13.4.1.1.2 step 2

1. `iss` is an allowed NRF / NRF Set.
2. Signature verifies with the NRF key (obtained via OAM or other means, §13.4.1.0); reject `alg` not in the allow-list.
3. Direct communication: `sub` equals the NF instance ID in the client TLS certificate's subjectAltName.
4. `aud` matches own NF instance ID or own NF type.
5. If present: S-NSSAI/NSI lists intersect; NF Set ID / NF Service Set ID match.
6. `scope` (and additional scope) covers the operation being invoked.
7. `exp` checked; `iat` may be checked per policy.
8. If a CCA is present it may be verified, and its `sub` must equal the token's `sub`.

## Client credentials assertion (CCA) — TS 33.501 §13.3.8

JWT signed with JWS; claims `sub` (consumer instance ID), `iat`, `exp`, `aud` (`"NRF"` and/or producer
NF type); header `x5u` or `x5c`. NRF validates both `iat` and `exp`; a producer validates `exp` and may
validate `iat`; `aud` must match the receiver; the instance ID must match the signing certificate. Not
usable in roaming without cross-certification.

## Transport precondition

If the PLMN uses token-based authorization, transport-layer protection is mandatory (TS 33.501
§13.3.2) — pair every OAuth2 review with skill `sbi-tls-hardening`.

## Where to look in this repo

`nfs/nrf/src/main.cpp` (token issue), `libs/sbi-core/src/jwt.cpp`, `libs/sbi-core/src/authorizer.cpp`
(producer checks), `libs/sbi-core/src/oauth2_client.cpp` (consumer caching).

Report each checklist item as verified-by-test, verified-by-reading, or not checked. Never claim
conformance for an item you did not check.
