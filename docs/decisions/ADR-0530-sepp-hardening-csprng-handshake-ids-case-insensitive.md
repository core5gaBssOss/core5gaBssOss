## ADR-0530: SEPP hardening: CSPRNG handshake ids, case-insensitive headers, bounded telescopic table, linear Via scan, bounded PRINS iePaths

**Date:** 2026-10-24. **Status:** Proposed.

**Context.** The remaining SEPP findings of REVIEW-5ae0ef1: SEC-11, SEC-12, SEC-14 (low) and the
suspected SUS-01 (medium) and SUS-03 (low). One subsystem, one increment.

**Decision.**
- **SEC-14:** N32 handshake ids from `RAND_bytes` (8 bytes, hex), like `random_n32f_context_id`;
  they correlate N32-f to N32-c among a peer's contexts.
- **SEC-11:** header names compared case-insensitively (RFC 9110 5.1): the consumer-PLMN check on
  `Authorization` (TS 29.573 step 6) and PRINS `open()`, which now lower-cases reconstructed header
  names before the POLICY_MISMATCH check (the policy's names are lower case).
- **SEC-12:** `TelescopicTable` keeps at most `telescopic_table_max` mappings (config/sepp.json,
  default 10000; GUI schema regenerated), evicting the oldest. The label is a pure function of the
  FQDN, so an evicted FQDN gets the same label when it is mapped again.
- **SUS-01:** `via_sepps` is a linear scan instead of `std::regex` (whose recursive matcher
  overflowed the stack at ~30 KB in the review's reproduction).
- **SUS-03:** PRINS body reconstruction refuses an iePath whose array index skips elements
  (`sepp::ie_path_within_bounds`), so `/x/100000000` cannot allocate a 100M-element array.

**Rejected alternatives.**
- *Caching only FQDNs of configured peers' domains (SEC-12).* Local NFs legitimately ask for any
  foreign FQDN's label; a cap keeps that working.
- *A maximum header length for Via (SUS-01).* Hides the cause; the linear scan removes it.

**Disclosed.** No nghttp2 header-size limit is set on the SEPP's servers (a general sbi-core
matter); the PRINS reconstruction bounds array indices but not nesting depth.

**Verified (2026-10-24).** See below.
