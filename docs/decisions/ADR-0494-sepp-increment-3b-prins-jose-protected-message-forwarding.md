## ADR-0494: SEPP increment 3b -- PRINS JOSE-protected message forwarding on N32-f (TS 29.573 5.3.2, TS 33.501 13.2.4)

**Date:** 2026-10-14. **Status:** Accepted -- built (completes the PRINS increment begun in ADR-0493).

**Context.** ADR-0493 negotiated PRINS and agreed the N32-f master key; traffic to a PRINS peer was
refused 504. This increment carries it: the sending SEPP reformats the NF's HTTP message, protects
it with JWE and POSTs it to the peer's `{apiRoot}/n32f-forward/v1/n32f-process`; the receiving SEPP
verifies, decrypts, rebuilds and relays it, and protects the answer the same way. Specs: TS 29.573
j70 5.3.2, 6.2 (TS29573_JOSEProtectedMessageForwarding.yaml, REL-19, added to codegen), TS 33.501
j70 13.2.4, RFC 7516, RFC 6901.

**Decisions.**
1. Reformatting (TS 29.573 6.2.5.2, TS 33.501 13.2.4.3): DataToIntegrityProtectBlock = metaData
   (receiver's n32fContextId, 64-bit hex messageId, authorizedIpxId "NULL" -- no RI is authorized),
   requestLine (method, scheme, authority, path, protocolVersion "2", queryFragment) or statusLine,
   headers, payload (the JSON body flattened to RFC 6901 leaf pointers). Each value to protect goes
   into DataToIntegrityProtectAndCipherBlock.dataToEncrypt and is replaced by `{"encBlockIndex":n}`
   (in a path segment, by that object's JSON text, with pathQueryProtectInd [URI_PATH]).
2. What is protected: the selected protection policy (ADR-0493, the responder's, applicable both
   ways) -- BODY / HEADER / URI_PATH IEs of a matching apiSignature + method whose type is in the
   data-type encryption policy -- plus, irrespective of policy, the types of TS 33.501 5.9.3.3
   (AUTHENTICATION_MATERIAL, KEY_MATERIAL, LOCATION, AUTHORIZATION_TOKEN) and always the
   Authorization header.
3. JWE: Flattened JSON Serialization; protected header {"alg":"dir","enc":<A128GCM|A256GCM>}; `aad`
   = BASE64URL(DataToIntegrityProtectBlock); AEAD AAD = BASE64URL(protected) "." BASE64URL(aad)
   (RFC 7516 5.1 step 14); plaintext = DataToIntegrityProtectAndCipherBlock (empty when nothing is
   encrypted); nonce = IV salt (8) || SEQ (32-bit, per IV salt) (TS 33.501 13.2.4.4.1).
4. **Which N32-f context id enters N32-KDF** (13.2.4.4.1 does not say which of the two): the one
   the message carries in its metadata -- by TS 29.573 6.2.5.2.9 the *receiving* SEPP's id. Both
   SEPPs see it on every message, so both derive the same key without extra state. A request from
   the N32-c initiator uses the "parallel_request" key, its answer "parallel_response"; requests
   from the responder the "reverse_*" keys.
5. Receiving SEPP (13.2.4.7): unknown context -> 403 CONTEXT_NOT_FOUND; a modificationsBlock -> 403
   UNSPECIFIED (no RI is authorized); tag failure / unparseable blocks -> 403 UNSPECIFIED plus an
   N32-f error report over N32-c (INTEGRITY_CHECK_FAILED / MESSAGE_RECONSTRUCTION_FAILED, 5.2.5); an
   IE the policy protects arriving in clear -> 403 POLICY_MISMATCH; a Bearer token whose
   consumerPlmnId is not the context's remote PLMN -> 403 PLMNID_MISMATCH (TS 29.573 5.3.2.1 step 6).
   The target NF is requestLine.authority in this PLMN, reached via `local_nf_routes` as for TLS;
   an unreachable NF becomes a protected 504 answer. The response's metadata must name this SEPP's
   context and the request's messageId.

**Disclosed.**
- No roaming intermediaries: authorizedIpxId is "NULL", modificationsBlock is refused, JWS (ES256)
  is negotiated but never used, and RI modification policies are not checked.
- Not built: URI_PARAM and MULTIPART_BINARY protection; callback-based policy lookup by
  3gpp-Sbi-Callback (notification APIs); gzip on N32-f; the JOSE OPTIONS operation; replay
  detection on received SEQ / messageId; the "continue with the old cipher suite for a limited time"
  NOTE; renegotiation. Non-JSON bodies cannot be reformatted (answered 500).
- Reconstruction of a flattened body cannot tell an array from an object with numeric keys: a
  numeric pointer token rebuilds an array (nlohmann's json_pointer rule). No 3GPP body seen here
  uses numeric object keys.
- statusLine carries the status code as digits ("201").
- The Bearer token's claims are read without verifying its signature (the producer verifies it);
  only the PLMN comparison uses them.

**Rejected alternatives.** Deriving with the sender's own context id: equally possible from the
text, but the metadata id is the one value guaranteed identical at both ends of every message.
Reformatting with hand-written JSON for the blocks: the YAML defines them, so the generated DTOs are
used (codegen gained the full C++ keyword list for FlatJweJson.`protected`).

**Verified.** `SeppN32.PrinsRequestRoundTripsAndHidesProtectedValues` (SUCI and token absent from
the clear part; nonce = salt || 7; tampered aad, flipped ciphertext and another context's key all
fail), `PrinsPolicyMismatchAndResponses` (POLICY_MISMATCH; A256GCM 201 response with 5gAuthData
protected; key directions), `Base64UrlRoundTrips` (incl. the RFC 4648 vector); structural
conformance of N32fReformattedReqMsg; `SeppEndToEnd.PrinsAgreesOnOneKeyAndForwardsJoseProtected`
(two real SEPPs, PRINS: PLMN B's real NRF answers through JOSE-protected N32-f with an encrypted
Authorization header; a token of PLMN 001/09 -> 403 PLMNID_MISMATCH; termination on shutdown).

**Review fixes (2026-10-15, independent review).** (1) The per-IV-salt 32-bit SEQ could wrap
silently, repeating a nonce under one key; at exhaustion the context now refuses to send (503) until
renegotiated. (2) Each N32-f error report spawned an unbounded detached thread; at most four are in
flight, the excess dropped and logged. Exchange-params gets the same sender binding as ADR-0491's
fix, so a partner cannot obtain another partner's PRINS master key.
