## ADR-0552: Adopt SBI TLS and OAuth2 review skills adapted from Anthropic-Cybersecurity-Skills (Apache-2.0)

**Date:** 2026-11-01. **Status:** Proposed.

**Context.** The user asked which skills from mukul975/Anthropic-Cybersecurity-Skills would benefit the project, and approved starting with TLS and OAuth2 because they map most directly to SBI security. The upstream skills are written for generic web services (nginx/Apache, user-facing OAuth). Several of their recommendations contradict the 3GPP SBI security profile, so copying them would push reviews toward non-conformant "fixes".

**Decision:**

1. **Two project skills are added, adapted from upstream commit `54a798831d2266a3ca61ce68a7acb80b81160d57`** (Copyright 2026 mukul975, Apache-2.0; skill author mahipal):
   - `.claude/skills/sbi-tls-hardening/` is adapted from `configuring-tls-1-3-for-secure-communications`.
   - `.claude/skills/sbi-oauth2-review/` is adapted from `configuring-oauth2-authorization-flow`.
2. **Every rule in them cites a clause read from the local spec text:**
   - TS 33.501 §13.1.0, §13.3 (§13.3.1–§13.3.2, §13.3.8) and §13.4.1 (§13.4.1.0–§13.4.1.1.3).
   - TS 33.210 §6.2.1–§6.2.3 and §6.3.3.
   - `TS29510_Nnrf_AccessToken.yaml` (REL-19) for `AccessTokenReq`, `AccessTokenRsp`, `AccessTokenClaims` and `AccessTokenErr`.
3. **3GPP wins over the upstream advice.** Each skill opens with a table of the places where they differ:
   - TLS 1.2 is mandatory alongside 1.3.
   - `ffdhe2048`, DHE and `psk_ke` are prohibited.
   - Only the `client_credentials` grant is used: no PKCE, refresh tokens, revocation or DPoP.
   - `sub` is bound to the SAN of the client certificate.
   - `aud` is the producer's NF type or instance ID.
   - Token-based authorization requires transport-layer protection.
4. **Attribution:**
   - Each SKILL.md carries the upstream URL, the commit, the copyright and a statement that it was rewritten.
   - `NOTICE` records the adapted files.
   - The Apache-2.0 text is already in `LICENSES/Apache-2.0.txt`. Upstream has no NOTICE file at that commit (HTTP 404), so there is no upstream NOTICE text to carry forward.

**Disclosed.**

- **These are review aids, not conformance evidence.** No code was audited against them in this ADR. The skills tell the reviewer to report each item as verified-by-test, verified-by-reading or not checked.
- **TS 33.310 §6.1.3c (the SBA certificate profile) has not been extracted locally.** The TLS skill cites it only through TS 33.501 §13.1.0 and tells the reader to fetch and read it before asserting SAN or EKU rules.
- **The LI interfaces in `libs/li-core` are not SBI** and are out of scope for these skills.

**Rejected alternatives.**

- **Copying the upstream skills verbatim.** They recommend TLS 1.3 only, PKCE, refresh-token rotation, a revocation endpoint and DPoP. All of these are non-conformant or not applicable on the SBI.
- **Also adopting `detecting-oauth-token-theft` now.** It targets user sessions and IdP logs. It was deferred for future implementation until the SBI has the audit telemetry it would read.
- **Writing the skills from scratch with no attribution.** Their structure (checklist, verification commands) comes from upstream, and Apache-2.0 §4 requires attribution and a statement of changes.
