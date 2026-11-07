## ADR-0562: MCP server over Streamable HTTP with Keycloak-issued tokens, in addition to stdio

**Date:** 2026-11-03. **Status:** Accepted.

**Origin.** From the reference-diagram review (MCP box reached over HTTPS with OAuth2).
Implemented after ADR-0556 to ADR-0558, and after ADR-0557 (it needs OPA for scopes). See
ADR-0556 "Origin".

**Context.** The MCP tool server (ADR-0331) speaks JSON-RPC 2.0 over stdio only: no port, no TLS,
no auth, because the caller runs on the same host as a child process. An agent on another host
cannot use it.

**Decision:**
1. Add MCP's **Streamable HTTP** transport as an option. stdio stays the default and is
   unchanged.
2. HTTP is served only over TLS 1.3 (OpenSSL 3.x, the project's existing TLS stack).
3. The server is an OAuth 2.x resource server per the MCP authorization specification.
   **Keycloak** (ADR-0441) is the authorization server; tokens are audience-bound to the MCP
   server, and a token for another audience is refused.
4. The agent identity in the token selects the agent's scopes, decided by OPA (ADR-0557).
5. ADR-0331 rules are unchanged on both transports: read-only tools, PII audit written before an
   answer is returned, refuse to start without the audit database.
6. The exact MCP specification revision implemented is pinned in the implementation PR.

**Rejected alternatives.**
- The older HTTP+SSE MCP transport: superseded by Streamable HTTP in the MCP specification.
- A static API key per agent: no expiry, no audience binding, no central revocation.
- Exposing the stdio server through an SSH tunnel or socat: no per-agent identity reaches the
  audit log.
- Putting the MCP server behind the BFF: mixes operator sessions with agent tokens.

**Disclosed.**
- Design only. Off-host agents are an option; nothing in the project needs them today.
- Latency and concurrent-session limits: not measured.
