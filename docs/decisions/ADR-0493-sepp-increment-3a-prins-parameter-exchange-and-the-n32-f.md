## ADR-0493: SEPP increment 3a -- PRINS parameter exchange and the N32-f master key (TS 29.573 5.2.3, TS 33.501 13.2.2)

**Date:** 2026-10-14. **Status:** Accepted -- 3a built; 3b (JOSE-protected forwarding, 5.3.2) next.

**Context.** PRINS (application-layer security on N32) was listed after ADR-0491/0492; the user
asked to proceed. It splits naturally: 3a = everything on N32-c (selection, parameter exchange,
key agreement, termination), 3b = the N32-f JOSE forwarding. Specs: TS 29.573 j70 5.2.2-5.2.4,
TS 33.501 j70 13.2.2 / 13.2.4.4.1 / 13.2.4.9, TS 33.210 j30 6.3.2 (fetched), RFC 8446 7.5,
RFC 5869.

**Decisions.**
1. Selection (5.2.2): `security_capabilities` in config is this SEPP's preference order; the
   initiator offers it, the responder takes the first of its own list the initiator offered.
   `3gppSbiTargetApiRootSupported` is sent only when TLS is selected. Default stays `["TLS"]`.
2. Parameter exchange (5.2.3.2): JWE cipher suites A128GCM (shall) and A256GCM (should), JWS ES256
   only (TS 33.210 6.3.2, TS 33.501 13.2.4.9); the responder picks by its own order; each SEPP
   issues a random 16-hex-digit n32fContextId (TS 33.501 13.2.2.4.1). Collision: 409
   SECURITY_PARAM_EXCHANGE_COLLISION when the receiving SEPP's FQDN precedes. No PRINS capability
   context with the sender, or no suite in common: 403 NEGOTIATION_NOT_ALLOWED.
3. Key agreement (TS 33.501 13.2.2.2 step 4, 13.2.4.4.1): both SEPPs run the TLS exporter
   "EXPORTER_3GPP_N32_MASTER", empty context, 64 octets, on the N32-c connection that carries the
   cipher-suite exchange request -- the server side on the connection it arrived on, the client on
   the connection it was sent on -- so both hold the same master key. `libs/sbi-core` gains
   `Server::set_tls_exporters` / `Request::tls_exports` and `ClientRequest::tls_exports` /
   `ClientResponse::tls_exports` (curl's CURLINFO_TLS_SSL_PTR + SSL_export_keying_material, taken
   in CURLOPT_PREREQFUNCTION while the connection is live -- read after the transfer, curl no longer
   exposed the session of the cached HTTP/2 connection: found by the e2e test, the server side
   exported on every attempt while the client got nothing).
4. N32-KDF = HKDF-Expand(SHA-256)(master, "N32" || N32-Context-ID || label, L), OpenSSL
   EVP_PKEY_HKDF in expand-only mode; verified against RFC 5869 test case 1. (Used by 3b.)
5. Protection policy exchange (5.2.3.3): this SEPP's policy from config (`protection_policy`,
   default: AUSF ue-authentications POST -- supiOrSuci UEID, 5gAuthData AUTHENTICATION_MATERIAL --
   and data-type encryption of UEID, LOCATION, KEY_MATERIAL, AUTHENTICATION_MATERIAL,
   AUTHORIZATION_TOKEN, i.e. TS 33.501 5.9.3.3's mandatory set plus SUPI); each side stores the
   peer's; the responder answers with its own as the selected policy.
6. Termination (5.2.4): the request names the context the receiver issued; the answer carries the
   initiator's id; on SIGTERM a SEPP terminates its PRINS contexts (TLS ones still use NONE).
7. Until 3b: traffic towards a PRINS peer is refused 504 (never sent unprotected), and plain N32-f
   relaying is accepted only on TLS contexts.

**Disclosed.**
- "The first N32-c connection" (13.2.2.2): the export is taken from the connection carrying the
  cipher-suite exchange, which with a pooled client may be a later connection than the one that
  carried exchange-capability. Both SEPPs still export from one and the same TLS session, which is
  what the key agreement needs.
- 5.2.3.4 RI security information list: not exchanged (no roaming intermediaries here); security
  profiles (PSEPRO) not supported -- a request with neither ciphers nor policy gets 400.
- Which N32-f context id enters N32-KDF is not stated unambiguously in 13.2.4.4.1 (each SEPP has
  one); 3b will fix and disclose the choice.
- Both SEPPs log a 4-octet SHA-256 fingerprint of the master key (never the key) so that key
  agreement is observable; the e2e test compares them.
- Policies are stored, not yet enforced (enforcement is the JOSE reformatting of 3b).

**Rejected alternatives.** A JOSE library: cjose / others add a dependency for what is AES-GCM +
base64url + JSON here, and none exports TLS keying material anyway; OpenSSL is already linked.
Exporting at exchange-capability time: the parameter exchange may run on another connection, and
the responder learns PRINS only then; tying the export to the request that creates the context
keeps both ends on one session.

**Verified.** `SeppN32.HkdfExpandMatchesRfc5869TestCase1`, `PrinsIsSelectedByLocalPreference`,
`ParameterExchangeNegotiatesCiphersThenPolicy`; `structural_conformance` (SecParamExchReqData /
SecParamExchRspData against TS29573_N32_Handshake.yaml);
`SeppEndToEnd.PrinsAgreesOnOneKeyAndForwardsJoseProtected (renamed from PrinsParameterExchangeAgreesOnOneMasterKey, REVIEW-5ae0ef1 DOC-04)` (two real SEPPs: PRINS selected, ids
swapped, A128GCM, identical master-key fingerprints, forwarding refused 504, SEPP A's shutdown
terminates the context at SEPP B); the TLS and telescopic e2e tests unchanged.
