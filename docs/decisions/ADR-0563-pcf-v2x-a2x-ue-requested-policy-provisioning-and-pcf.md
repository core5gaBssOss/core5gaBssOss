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
