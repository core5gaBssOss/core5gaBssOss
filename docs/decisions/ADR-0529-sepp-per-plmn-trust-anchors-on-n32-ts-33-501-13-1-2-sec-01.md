## ADR-0529: SEPP per-PLMN trust anchors on N32 (TS 33.501 13.1.2, SEC-01)

**Date:** 2026-10-24. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SEC-01 (critical). The `sender`-to-certificate binding was fixed in
ADR-0491; what remained: the N32 server and clients trusted one lab CA for every peer, with no
per-PLMN trust anchors, and the lab ran both partner SEPPs from one certificate naming both PLMNs.
User direction 2026-10-24: per 3GPP; spec problems to README.

**Decision.** TS 33.501 13.1.2:
- `sepp::TrustAnchors` (`nfs/sepp/src/trust_anchors.{hpp,cpp}`), from `config/sepp.json`
  `trust_anchors`: each anchor is a list of root certificates (PEM, relative to the certs directory)
  and a list of PLMN-IDs; a PLMN-ID in two anchors, an unreadable root or an empty anchor is a
  startup error.
- A certificate's PLMN-IDs are read from its dNSName SANs under the Home Network Domain
  `5gc.mnc<MNC>.mcc<MCC>.3gppnetwork.org` (TS 23.003 28.2). The remote SEPP's end-entity
  certificate is accepted only if its PLMN-IDs map to exactly one anchor and the chain OpenSSL built
  ends at one of that anchor's roots; otherwise the handshake fails. No PLMN-ID (e.g. an NF
  certificate) maps to no anchor and is refused.
- sbi-core: `TlsConfig::verify`, an extra check run from OpenSSL's verify callback, on the server
  (Boost.Asio) and the client (libcurl `CURLOPT_SSL_CTX_FUNCTION`). Unset for every other NF.
- The SEPP applies it to the N32 server and to every client towards peer SEPPs (N32-c initiator,
  N32-f forwarding, error reports, shutdown teardown); its SBI side towards its own PLMN keeps the
  lab CA. OpenSSL builds chains against the union of all anchors' roots (a generated bundle); the
  per-anchor restriction is the extra check.
- The SEPP's own certificate is configuration (`cert_path`, `key_path`). The lab PKI issues one per
  PLMN: `sepp` (001/01) and `sepp-b` (001/02); CI and compose generate both.

**Rejected alternatives.**
- *One OpenSSL store per anchor, chosen by SNI.* The client side knows the expected peer, the
  server does not until it sees the certificate; checking the built chain against the anchor works
  the same in both directions.
- *Keeping the common lab certificate.* 13.1.2 rejects a certificate whose PLMN-IDs map to
  different anchors.

**Disclosed.**
- TS 33.310 6.1.3c.5.3.1 leaves the inter-PLMN SEPP certificate's Subject/SAN contents to GSMA
  FS.34, which this project does not have; the PLMN-ID is taken from the TS 23.003 SEPP FQDN form,
  as TS 33.310's inter-SNPN profile does (README "3GPP specification gaps").
- N32-f certificate PLMN-IDs being a subset of the N32-c certificate's (13.1.2) and RI identifiers
  in anchors are not implemented (SEC-02 / PRINS RI scope).
- SNPN (NID) identities are not read.
- In the lab both anchors use the same lab CA, so the root restriction is exercised by the unit
  tests (two independent CAs), not by the e2e test.

**Verified (2026-10-24).** See below.
