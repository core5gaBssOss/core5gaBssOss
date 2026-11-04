---
name: sbi-tls-hardening
description: Review or change TLS/mTLS on any SBI interface in this repo (sbi-core Http2Server, libcurl clients, Helm/compose certs) against the 3GPP TLS profile - TS 33.501 §13.1.0 and TS 33.210 §6.2. Use whenever touching TLS versions, cipher suites, groups, signature algorithms, certificate verification or SNI on SBI traffic.
---

# SBI TLS hardening (3GPP profile)

Adapted from `configuring-tls-1-3-for-secure-communications` in
[mukul975/Anthropic-Cybersecurity-Skills](https://github.com/mukul975/Anthropic-Cybersecurity-Skills)
at `54a798831d2266a3ca61ce68a7acb80b81160d57` (Copyright 2026 mukul975, Apache-2.0; skill author mahipal). Rewritten
for the 3GPP SBI; see ADR-0552 for the credit and what was dropped. **Where the upstream generic advice
and 3GPP disagree, 3GPP wins.**

## Where 3GPP overrides the upstream skill

| Upstream (generic web) | 3GPP SBI rule | Source |
|---|---|---|
| "TLS 1.3 only" | TLS 1.2 **and** TLS 1.3 shall both be supported | TS 33.210 §6.2.1 |
| Any HTTP/2 or HTTP/1.1 fallback | HTTP/2 per RFC 9113 over TCP, no HTTP/1.1 upgrade | TS 33.210 §6.2.1; TS 33.501 §13.1.0 |
| ffdhe groups acceptable | `ffdhe2048` shall not be supported; no DHE in TLS 1.2 | TS 33.210 §6.2.2, §6.2.3 |
| PSK resumption modes | TLS 1.3 `psk_ke` shall not be supported (only `psk_dhe_ke`) | TS 33.210 §6.2.2 |
| Server-only auth | All NFs shall support mTLS; clients shall send SNI | TS 33.501 §13.1.0 |

## Checklist

Version floor (TS 33.210 §6.2.1): SSL 1.0/2.0/3.0, TLS 1.0/1.1 and DTLS 1.0 shall not be supported.

TLS 1.3 (TS 33.210 §6.2.2):

- Follow RFC 8446 §9.1 mandatory-to-implement; `secp384r1` and `ecdsa_secp384r1_sha384` should be supported.
- `TLS_SHA256_SHA256` and `TLS_SHA384_SHA384` (integrity-only) shall not be supported.
- OCSP stapling should be supported.
- HTTP/2 servers shall not send a post-handshake `CertificateRequest` (RFC 9113 §9.2.3).

TLS 1.2 (TS 33.210 §6.2.3):

- Mandatory: `TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256`, `TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256`. Recommended: the `AES_256_GCM_SHA384` variants. AEAD + PFS suites only.
- Groups: P-256 shall, P-384 should; nothing below 256 bits except x25519; no DHE.
- Hashes: SHA-256 shall, SHA-384 should; no MD5, no SHA-1.
- Signature algorithms: ecdsa, rsa_pss_rsae, rsa_pkcs1 (rsa_pkcs1 not recommended).
- Null compression only. SNI shall be supported; Truncated HMAC shall not.
- Extended Master Secret (RFC 7627) shall be supported; renegotiation per RFC 5746; session resumption should be supported.

Certificates (TS 33.501 §13.1.0): follow the SBA certificate profile in TS 33.310 clause 6.1.3c.
That clause has not been extracted locally yet — run `tools/specs/fetch_3gpp_specs.py` and read it
before asserting any SAN/EKU rule; do not cite it from memory.

Use of TLS (TS 33.501 §13.1.0, §13.3.2): TLS shall be used within a PLMN unless network security is
provided by other means; if the PLMN uses token-based authorization, transport-layer protection is
mandatory.

## Where to look in this repo

- Server context: `libs/sbi-core/src/http2_server.cpp` (`SSL_CTX_set_min_proto_version`, `kTls12CipherList`, `set_verify_mode`).
- Clients: `libs/sbi-core` libcurl clients — check version floor, SNI and peer verification on.
- LI interfaces in `libs/li-core` are **not SBI** interfaces and are out of scope for this checklist.

## How to verify (measure, never assume — guardrail 6)

```bash
openssl s_client -connect <host:port> -tls1_1 </dev/null      # must fail
openssl s_client -connect <host:port> -tls1_2 -cipher 'ECDHE-ECDSA-AES128-GCM-SHA256' -servername <fqdn> </dev/null
openssl s_client -connect <host:port> -tls1_3 -groups ffdhe2048 </dev/null   # must fail
openssl s_client -connect <host:port> -tls1_2 -cipher 'AES128-SHA' </dev/null  # non-PFS: must fail
```

Report only what these commands returned. A clause not checked is reported as not checked.
