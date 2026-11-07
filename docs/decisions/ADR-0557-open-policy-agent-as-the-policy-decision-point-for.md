## ADR-0557: Open Policy Agent as the policy decision point for northbound and MCP tool authorisation

**Date:** 2026-11-03. **Status:** Accepted.

**Origin.** From the reference-diagram review (security row: mTLS + OIDC + OPA + Vault).
Second in the user-directed implementation order (after ADR-0556). See ADR-0556 "Origin".

**Context.** Authorisation decisions outside the SBI are written in C++ today:
- the BFF decides what an operator role may do (ADR-0422, ADR-0424, ADR-0441);
- the BSS REST services (TMF APIs) each check their caller;
- the MCP server decides which tools an agent may call from per-agent scopes in
  `config/mcp-server.json` (ADR-0331).
Each has its own rule format, and a rule change in the BFF or BSS services needs a rebuild
(guardrail 8: configuration over code).

**Decision:**
1. **Open Policy Agent** (Apache-2.0, CNCF graduated) becomes the policy decision point (PDP) for
   three places only: the BFF, the BSS TMF API services, and the MCP server.
2. The enforcement points (PEPs) stay in the existing C++ services. Each asks a local OPA sidecar
   over HTTP on localhost (`POST /v1/data/<package>/allow`) with input = authenticated principal
   (from the OIDC or mTLS identity already established), action, resource.
3. **Fail closed**: OPA unreachable, timeout, or an undefined result means deny.
4. Policies are Rego files under version control (`config/policy/`), unit-tested with `opa test`
   in CI, and loaded by OPA as a bundle. A policy change needs no rebuild.
5. The MCP per-agent scopes move from `config/mcp-server.json` into Rego data. ADR-0331's
   audit-before-answer rule is unchanged: a refusal is still written to `pii_access_audit` as
   `denied_scope` by the MCP server, which remains the PEP.
6. Out of scope, explicitly:
   - **SBI authorisation.** It stays NRF-issued OAuth2 access tokens checked by each NF
     (TS 33.501 §13.4.1, TS 29.510). OPA is never consulted on an NF-to-NF request.
   - **PCC/charging policy.** PCF decisions follow TS 23.503 and CHF decisions follow
     TS 32.290; OPA does not make or override them.

**Rejected alternatives.**
- Keep rules in C++ per service: every change is a rebuild, and three rule formats drift.
- Keycloak Authorization Services as the PDP: ties authorisation to the IdP and does not cover
  the MCP server's non-HTTP stdio callers.
- OPA embedded as a Go library: the services are C++; a sidecar keeps OPA out of the process.
- OPA on the SBI path (via a mesh): conflicts with the TS 33.501 token model, rejected in the
  diagram review.

**Disclosed.**
- Design only. No sidecar, policy or PEP change exists yet.
- Decision latency per request: not measured. It is added to every BFF, BSS and MCP call, so
  increment 1 must measure it before claiming anything.
- Increment order inside this ADR: MCP server first (rules already in config), then the BFF,
  then the BSS services.
