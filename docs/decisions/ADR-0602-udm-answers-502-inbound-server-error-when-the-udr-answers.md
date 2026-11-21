## ADR-0602: UDM answers 502 INBOUND_SERVER_ERROR when the UDR answers 503 or 429 (SBI-03, UDM part 2)

**Date:** 2026-11-21. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572). ADR-0534 left one
item open: the UDM's `AuthDataError::Unavailable` covered a token failure, an unreachable UDR, any
UDR status other than 200/404, and exhausted SQN compare-and-swap retries, and all of them became a
cause-less 503. Only a UDR 503 or 429 is covered by TS 29.500 clause 6.4.2.1. Splitting the enum was
deferred to "a later increment". This is that increment.

TS 29.500 (`specs/3gpp/MANIFEST.tsv`), clause 6.4.2.1:

> When an NF Service Consumer (e.g. AMF) issues a service request to an NF Service Producer (e.g.
> SMF), but the NF Service Producer cannot fulfil the request due to receiving a 503 or 429 status
> code from another NF Service Producer (e.g. PCF), the former NF Service Producer shall reject the
> service request from the NF Service Consumer with a 502 Bad Gateway HTTP error response including
> the "problemDetails" with the "cause" attribute set to "INBOUND_SERVER_ERROR".

Table 5.2.7.2-1, `INBOUND_SERVER_ERROR`, 502 Bad Gateway: "The request is rejected due to the
receipt of an 5xx error from an inbound server that the NF Service Producer accessed while
attempting to fulfil the request (see clause 6.4.2.1)."

**Decision:**

- `nfs/udm/src/udr_auth_source.hpp`: a new `AuthDataError::UdrOverloaded`, meaning "the UDR answered
  503 or 429".
- `nfs/udm/src/udr_auth_source.cpp`: `error_for_udr_status()` maps a UDR 503 or 429 to
  `UdrOverloaded` and any other status to `Unavailable`. It is used at the two places that look at a
  UDR status:
  - the QueryAuthSubsData GET that is neither 200 nor 404;
  - the ModifyAuthenticationSubscription PATCH that is neither 200, 204 nor 400.
- `nfs/udm/src/main.cpp`, `auth_data_problem()`: `UdrOverloaded` becomes 502 "Bad Gateway" with cause
  `INBOUND_SERVER_ERROR`. `auth_data_problem()` is the only place that answers this error. Four
  Nudm_UEAU operations use it:
  - GenerateAuthData, including its resynchronisation path;
  - GenerateAv (HSS);
  - GenerateGbaAv;
  - GenerateProseAV.

  `TS29503_Nudm_UEAU.yaml` lists `'502'` for all four.
- Unchanged: a token failure, an unreachable UDR, any other UDR status, and exhausted
  compare-and-swap retries are still the cause-less 503 (ADR-0534).
- Test: `UdmUdrOverload.Udr503Or429IsBadGatewayInboundServerErrorOtherFailuresStay503`. It runs a real
  NRF and UDM. The UDR is an in-process `sbi_core::http2::Server` (TLS 1.3 + mTLS), and the UDM is
  pointed at it through `UDM_UDR_BASE_URL`. The stub answers QueryAuthSubsData with 503, then 429,
  then 500. The test asserts 502 `INBOUND_SERVER_ERROR` for the first two and a cause-less 503 for
  the 500.

**Verified (2026-11-21, master machine, main checkout at this commit).** `udm`, `nrf` and
`integration_tests` build clean (0 failed steps). `integration_tests
--gtest_filter='UdmUdrOverload.*'` passes (1 test). With `nfs/udm/src/main.cpp`,
`udr_auth_source.cpp` and `udr_auth_source.hpp` restored from cc995945 and `udm` rebuilt, the test
fails: the UDR 503 and the UDR 429 both come back as 503 with an empty `cause`. Targeted test only,
not a full `ctest`; the compare-and-swap PATCH path has no test of its own. CI on PR #53 (run 44873516296, rebased onto 12b9c268) passed: build, lint,
asan-ubsan and tsan (2026-11-21). Merged as ea82f83f.

**Disclosed.**

- The exception in 6.4.2.1 (a V-SMF/I-SMF relays the 503/429 with `remoteError`) concerns the SMF.
  It does not apply to the UDM.
- Only a UDR 503 or 429 is mapped to 502. A UDR 500 or 504 is also a "5xx error from an inbound
  server" under the table's wording. But clause 6.4.2.1 specifies the 502 only for 503 and 429, so
  those statuses stay 503, as ADR-0534 scoped the item.
- A 503 or 429 answered by the NRF to the UDM's token request is not mapped: `OAuth2Client` reports
  a failure without the status. It stays 503.
- The PATCH (compare-and-swap) site has no test of its own. It is the same helper and the same
  mapping as the GET.

**Rejected alternatives.**

- *502 for every non-200/404 UDR status.* Clause 6.4.2.1 names 503 and 429 only, so this would widen
  the rule beyond the text.
- *Relaying the UDR's 503 or 429 unchanged.* Clause 6.4.2.1 allows that only for the V-SMF/I-SMF
  exception.
- *`NF_CONGESTION` (503) for an overloaded UDR.* That cause reports the producer's own congestion,
  not its upstream's.
