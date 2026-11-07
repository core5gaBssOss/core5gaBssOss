## ADR-0563: PCF V2X/A2X UE-requested policy provisioning and PCF-initiated modification (ADR-0551 increment 3)

**Date:** 2026-11-04. **Status:** Proposed.

**Context.** ADR-0551 split the PCF V2X/A2X procedure list P1-P7 into three increments. Increment 1 covered P1, P2 and P5, and increment 2 (ADR-0555) covered P3 and P4. Increment 3 is P6 + P7. On 2026-11-04 the user approved this procedure list:

- **P6a:** UE-requested V2X/A2X policy provisioning (TS 24.587 §5.3.2 for V2X; TS 24.577 §5.3.2 for A2X, which applies TS 24.587 §5.3.2.3-§5.3.2.6 clause by clause).
- **P7:** UE Policy Association Modification initiated by the PCF (TS 23.502 §4.16.12.2; TS 29.525 §4.2.4.2, §4.2.4.5 for V2X, §4.2.4.8 for A2X).

When ADR-0551 was written, its P6 row cited TS 24.587 only. TS 24.588 is the V2X UE policies spec (V2XP coding), not A2X. The A2X protocol spec is TS 24.577, and it does define a UE-requested A2X procedure, so P6 covers both V2X and A2X. The ADR-0551 row has been corrected to say so.

**Decision:**

1. **P6a: UE POLICY PROVISIONING REQUEST** arrives on the increment-2 N1MessageNotify callback (N1 message class `UPDP`). It carries a PTI and the Requested UE policies IE: PC5, Uu or both.
   - **Accepted** (TS 24.587 §5.3.2.3): the PCF builds a MANAGE UE POLICY COMMAND per TS 24.501 D.2.1, with the PTI set to the PTI of the request (TS 24.501 D.2.1.2 a). The command carries only the policy kinds the UE requested. It reuses the increment-2 delivery path (Namf_Communication N1N2MessageTransfer).
   - **Not accepted** (TS 24.587 §5.3.2.4): the PCF sends UE POLICY PROVISIONING REJECT with a UPDS cause:
     - #31: request rejected, unspecified.
     - #32: service option not supported, e.g. the association is not V2X/A2X-authorized under P1.
     - #34: service option temporarily out of order, e.g. the UDR read fails.
     - #35: PTI already in use.
     - #95-111: protocol errors.
   - **Abnormal case on the network side** (TS 24.587 §5.3.2.5): if the REJECT is reported as not transmitted, the PCF aborts the procedure and keeps no state.
2. **P7: PCF-initiated modification** (TS 23.502 §4.16.12.2):
   - **Triggers:** a UDR notification (steps 1a/1b; the increment-2 udr-app-data callback) or a PCF local decision (steps 1c/1d).
   - **Policy:** re-derive V2XP/A2XP (step 2) and re-deliver it in a MANAGE UE POLICY COMMAND (step 3). The PTI is newly allocated, since the UE did not ask for it.
   - **Policy triggers:** when they change, send Npcf_UEPolicyControl_UpdateNotify, POST `{notificationUri}/update` with a `PolicyUpdate` body (steps 5/6). The 204, 200, 307 and 404 responses are handled per TS 29.525 §4.2.4.2.
   - **N2 PC5 policy:** the V2X N2 PC5 policy (increment 1, P5) is re-sent when the re-derived content changes.
3. **Tests:** each step above gets a test derived from its call flow (ADR-0551 decision 1).

**As implemented (P6a and the P7 delivery-failure path).**

- **P6a** is in `nfs/pcf/src/ue_policy_delivery.cpp` and the N1MessageNotify route in `nfs/pcf/src/main.cpp`. It is tested in `tests/integration/test_pcf_ue_policy_delivery.cpp`.
- **P7, delivery-failure path** (TS 23.502 §4.2.4.3, executed with the §4.16.12.2 modification):
  - A UE policy delivery fails in one of two ways. Either the AMF sends an N1N2MessageTransfer failure notification to the new callback `POST {kNotifyRoot}/n1n2-failure/{polAssoId}`; the transfer request now carries `n1n2FailureTxfNotifURI` (TS 29.518). Or the AMF answers the transfer with 409 or 504.
  - In either case the PCF frees the PTI and marks the association `delivery_failure_pending`. It then adds `CON_STATE_CH` to the association's triggers and sends Npcf_UEPolicyControl_UpdateNotify (`POST {notificationUri}/update`, `PolicyUpdate{resourceUri, triggers}`). The UpdateNotify is sent only when the trigger was not already provisioned.
  - When an Npcf_UEPolicyControl_Update reports `CON_STATE_CH` with `connectState` CONNECTED and the flag is set, the PCF clears the flag and delivers again with a newly allocated PTI. If that delivery fails again, the flag is set again and no second UpdateNotify is sent.
- **Why CON_STATE_CH:** TS 23.502 §4.2.4.3 has the PCF subscribe to the UE's connectivity state after a failed delivery, so that it can retry when the UE becomes reachable. In the TS 29.525 YAML, that subscription is the `RequestTrigger` value `CON_STATE_CH`, and the report is `PolicyAssociationUpdateRequest.connectState` (CmState).
- **UpdateNotify responses** (TS 29.525 §4.2.4.2):
  - **204/200:** success.
  - **307/308:** the PCF resends once to the `Location` URI. Later notifications still go to the original `notificationUri`.
  - **404:** the PCF resends once to the same URI, because the alternate-AMF addresses (`altNotif*`) are deferred for future implementation.
- **Tests:** all derived from the §4.2.4.3 flow.
  - A malformed failure notification gets 400, and one for an unknown association gets 404 ProblemDetails (TS 29.518 YAML callback responses).
  - A valid one gets 204, followed by an UpdateNotify that the mock answers with 307 and that is then delivered at the redirected URI.
  - A CONNECTED update while the AMF answers 504 produces a re-attempt and no second UpdateNotify.
  - A CONNECTED update while the AMF answers 200 re-delivers the expected COMMAND with a network PTI (0x80-0xFE).
  - A further CONNECTED update produces no delivery, because the flag has been cleared.

**Simplified or non-conformant (disclosed).**

- **A2XI handling (spec inconsistency):** TS 24.577 §5.3.2.2b and TS 24.587 §8.3.2 disagree on how the A2X indicator in the Requested UE policies IE is split between PC5 and Uu. The PCF reads A2XI=1 as a request for both, and sends both A2X PC5 and A2X Uu parameters. This is recorded in README.
- **409 and 504 treated as "UE not reached":** the TS 29.518 YAML lists both codes, but no TS 29.518 prose is available offline, so the per-cause semantics were not verified. Both are treated as a delivery failure.
- **Not atomic:** the association record is read, modified and written back without a compare-and-swap. A concurrent Npcf_UEPolicyControl_Update can therefore race the failure handling, and one of the two writes can be lost.
- **No NRF token on the failure callback:** the `n1n2-failure` route is `RouteAuth::None`, as callback routes are across the project (TS 33.501 §13.4.1.0, ADR-0499).
- **Pre-existing non-conformance, not fixed here:** the Update handler overwrites the association's provisioned triggers with the triggers the AMF reported (`association.triggers = body->triggers`). A later update that reports only PLMN_CH therefore drops CON_STATE_CH. Fixing it means keeping the provisioned and the reported triggers separate; this is deferred for future implementation.
- **Not yet implemented** (still in increment 3): the remaining P7 branches, namely re-deriving and re-delivering V2XP/A2XP after a UDR notification, and re-sending the V2X N2 PC5 policy.
- **Verification:** the code and the test pass a syntax-only compile and clang-format-18. They were not built or run locally, because a full worktree build takes about 2 hours and the CI runner shares this host. The PR's CI is the first execution.

**Deferred for future implementation (disclosed).**

- **P6b, UE-initiated UE STATE INDICATION** (TS 24.501 D.2.2). Deferred at the user's direction on 2026-11-04. It reaches the PCF in the UE policy container of the registration-time Create, not on the N1 callback, so it widens the scope of this increment. The UE policy classmark IE stays deferred with it.
- **CHF-triggered modification** (TS 23.502 §4.16.12.2 step 1e).
- **Network-side supervision and retransmission** of the COMMAND (T3501); the UE-side T5040 retransmission is the UE's job.
- `interGroupId` / `anyUeInd` group data, roaming (H-PCF to V-PCF), the A2X N2 PC5 policy, and the BRID, DDAA, C2 and GBDAAA families. These were carried over from ADR-0555.
- **Still non-conformant:** the `supis` query-encoding issue from ADR-0555 stays open.

**Rejected alternatives.**

- **Include P6b now.** Rejected by the user: it is a different entry point (the Create body), so it is a separate increment.
- **Limit P6 to V2X**, based on the absence of the procedure in TS 24.588. Rejected: that was the wrong spec. TS 24.577 §5.3.2 defines the A2X procedure.
- **Answer a PROVISIONING REQUEST with a newly allocated PTI.** Rejected: TS 24.501 D.2.1.2 a) requires the PTI of the request.
