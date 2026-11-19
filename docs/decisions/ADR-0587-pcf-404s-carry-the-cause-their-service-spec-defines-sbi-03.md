## ADR-0587: PCF 404s carry the cause their service spec defines (SBI-03, PCF part)

**Date:** 2026-11-19. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`): the PCF answered 34 of its 404s with no
`cause`. Each PCF service has its own stage-3 spec, and each one lists the 404 application error
for its own resource. TS 29.507, 29.534, 29.537, 29.543, 29.554, 29.594 and 29.523 were fetched
for this (MANIFEST.tsv); TS 29.512, 29.514 and 29.525 were already in hand. This is v0.2.0 item 4
(ADR-0572), PCF part, after NRF (ADR-0577), NEF (ADR-0578), UDR (ADR-0583) and UDM (ADR-0584).

**Decision:** 33 of them now carry the cause its service's table defines:

| Service (PCF as server) | Spec, table | Cause |
|---|---|---|
| Npcf_AMPolicyControl | TS 29.507 table 5.7.3-1 | `POLICY_ASSOCIATION_NOT_FOUND` |
| Npcf_SMPolicyControl | TS 29.512 table 5.7.3-1 | `POLICY_ASSOCIATION_NOT_FOUND` |
| Npcf_PolicyAuthorization | TS 29.514 table 5.7.3-1 | `APPLICATION_SESSION_CONTEXT_NOT_FOUND` |
| Npcf_UEPolicyControl | TS 29.525 table 5.7.3-1 | `POLICY_ASSOCIATION_NOT_FOUND` |
| Npcf_AMPolicyAuthorization | TS 29.534 table 5.7.3-1 | `APPLICATION_AM_CONTEXT_NOT_FOUND` |
| Npcf_MBSPolicyControl | TS 29.537 table 6.1.7.3-1 | `MBS_POLICY_ASSOCIATION_NOT_FOUND` |
| Npcf_MBSPolicyAuthorization | TS 29.537 table 6.2.7.3-1 | `MBS_SESSION_POL_AUTH_CTXT_NOT_FOUND` |
| Npcf_PDTQPolicyControl | TS 29.543 table 6.1.7.3-1 | `PDTQ_POLICY_NOT_FOUND` |
| Npcf_BDTPolicyControl | TS 29.554 table 5.7.3-1 | `BDT_POLICY_NOT_FOUND` |
| Npcf_EventExposure GET subscription | TS 29.500 table 5.2.7.2-1 | `SUBSCRIPTION_NOT_FOUND`, as its PUT and DELETE already did |
| Spending-limit `statusNotify` callback for an untracked SM policy | TS 29.500 table 5.2.7.2-1 | `SUBSCRIPTION_NOT_FOUND` |

Tests: the existing PCF integration tests that already expected a 404 now also assert the cause
(AM and SM policy, UE policy, AM policy authorization, MBS policy and MBS policy authorization,
PDTQ, BDT and event exposure). `PcfIntegration.MissingResourceIs404AndTamperedTokenIs401` gains a
GET of an unknown app session that expects `APPLICATION_SESSION_CONTEXT_NOT_FOUND`.

Disclosed:
- TS 29.523 defines no 404 cause of its own. The event-exposure GET uses the TS 29.500 generic
  cause, as the PUT and DELETE in the same API already did.
- The spending-limit callback is the PCF acting as a consumer that receives a TS 29.594
  notification. TS 29.594 lists 404 but no cause. The TS 29.500 entry "the notification request is
  rejected because the subscription is not found" is the reading used, and TS 29.500 clause 6.2.3
  tells the CHF to end the subscription on it.
- One 404 stays without a cause: the N1N2 transfer failure callback for an unknown UE policy
  association (TS 29.518 lists 404 there and defines no cause, and the callback is not a
  subscription, so `SUBSCRIPTION_NOT_FOUND` would not fit).
- Compile-checked locally only (this host is a CI runner). CI on PR #36 passed: build, lint, asan-ubsan and tsan (2026-11-19).

**Rejected alternatives.**
- `POLICY_CONTEXT_NOT_FOUND` for SM policy 404s: TS 29.512 table 5.7.3-2 gives it to the SMF
  acting as a server for notifications, not to the PCF.
- One generic cause such as `RESOURCE_NOT_FOUND` for every PCF 404: TS 29.500 has no such cause,
  and the per-service tables are specific.
