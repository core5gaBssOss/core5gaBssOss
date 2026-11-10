## ADR-0555: PCF V2XP/A2XP UE policy delivery over N1 (ADR-0551 increment 2: P3, P4)

**Date:** 2026-11-02. **Status:** Closed (work pushed; last citing commit d05994be on origin/main, 2026-11-10). Proposed.

**Context.** ADR-0551 split the approved PCF V2X/A2X procedure list P1-P7 into three increments. Increment 1 (P1, P2, P5) gave the PCF eligibility from the UDM and the N2 PC5 policy, but it still sent the UE no V2XP or A2XP. Increment 2 adds:

- **P3:** read the V2X/A2X service parameters an AF provisioned through the NEF from the UDR application data, and subscribe to their changes. The source is TS 29.519 `serviceParamData`, with the fields `paramOverPc5`, `paramOverUu`, `a2xParamsPc5` and `a2xParamsUu`.
- **P4:** deliver those parameters to the UE in a MANAGE UE POLICY COMMAND (TS 24.501 Annex D.5.1). The command goes through Namf_Communication_N1N2MessageTransfer with N1 message class `UPDP` (TS 29.518). The UE's MANAGE UE POLICY COMPLETE or COMMAND REJECT comes back through N1MessageNotify.

Stage 2: TS 23.287 §6.2.2, TS 23.256 §6.2.7.1, TS 23.502 §4.2.4.3. Stage 3: TS 29.519, TS 29.518, TS 29.525 §4.2.2.2.4, TS 24.501 Annex D, TS 24.588 (V2XP), TS 24.578 (A2XP).

**Decision:**

1. **New module `nfs/pcf/src/ue_policy_delivery.{hpp,cpp}` (namespace `pcf::updp`).** It holds the UE policy delivery codec, the per-association state (`DeliveryStore`) and the outbound calls. The NAS codec is local to the PCF, because no other NF builds UE policy containers.
2. **P3 read.** GET `{udr}/nudr-dr/v2/application-data/serviceParamData?supis=<supi>`. Each base64 string in `paramOverPc5` / `paramOverUu` becomes one V2XP info, of type V2X Pc5 (1) or V2X Uu (2). Each string in `a2xParamsPc5` / `a2xParamsUu` becomes one A2XP info, of type A2X Pc5 (1) or A2X Uu (5).
3. **P3 subscription.** POST `{udr}/nudr-dr/v2/application-data/subs-to-notify` with `dataFilters: [{dataInd: "SVC_PARAM", supis: [supi]}]`. The PCF keeps the `Location` and DELETEs it when the association is deleted. On a change notification (an `ApplicationDataChangeNotif` array whose `serParamData.supi` matches), the PCF re-reads the data and re-delivers it for every authorized association of that UE.
4. **P4 subscription and delivery.**
   - The PCF subscribes once per association at the AMF: `ue-contexts/{supi}/n1-n2-messages/subscriptions` with `n1MessageClass: "UPDP"` and a per-association `n1NotifyCallbackUri`. It DELETEs that subscription on association delete.
   - Delivery is a multipart N1N2MessageTransfer: a JSON part with `n1MessageContainer{n1MessageClass: "UPDP", n1MessageContent{contentId}}` and an `application/vnd.3gpp.5gnas` part.
5. **PTI (TS 24.501 D.2).** The PCF allocates the PTI from 0x80-0xFE. A value is never reused while it is pending on any association, and it is released when the UE answers with that PTI.
6. **Gating.** P3 and P4 run only for an association whose V2X or A2X authorization from increment 1 (P1) succeeded, and they fail closed. All outbound calls run on the existing bounded worker (`pcf::BoundedWorker`), never on an SBI server thread.
7. **Notification routes skip OAuth 2.0**, following TS 33.501 §13.4.1.0 and ADR-0499. These are the UDR change notification and the AMF N1MessageNotify.
8. **Configuration.** `udr_base_url` and `ue_policy_plmn` (the PLMN of the UE policy section when the Create carries no `servingPlmn`) are set in `config/pcf.json`.

**Disclosed simplifications (deferred for future implementation).**

- **Content encoding is assumed.** TS 29.519 types the service parameter fields as strings. This build treats each string as base64 of an already-encoded V2XP/A2XP info contents, and makes each string one info. A string that is not base64 is skipped and logged.
- **Length fields.** The part contents length covers the part-type octet plus the contents (TS 24.501 Table D.6.2.1 NOTE 2). Each info is written as a type octet, a 2-octet length and the contents. Both readings should be checked against the next spec in hand.
- **One UE policy section.** The V2X/A2X section always uses UPSC 0x0001, under one PLMN sublist, and is replaced on each delivery. There is no UPSC allocation across sections or PLMNs.
- **PLMN source.** The PLMN is the Create's `servingPlmn`, falling back to `ue_policy_plmn` from config.
- **SUPI filter applied in the PCF.** The PCF also drops any `serviceParamData` entry whose `supi` is not the UE's, in addition to the `supis` query, so a UDR that ignores the query cannot leak another UE's parameters.
- **PTI mismatch.** A COMPLETE or REJECT whose PTI is not the pending one is logged and still answered with 204. The PCF does not reject it.
- **Callback paths are this project's choice:** `/npcf-ue-policy-control/v1/callbacks/udr-app-data` and `.../callbacks/n1-msg/{polAssoId}`.
- **Deferred for future implementation:**
  - T3501 supervision and retransmission of the COMMAND.
  - The UE policy classmark and VPS URSP IEs.
  - UE-initiated UE STATE INDICATION / PROVISIONING REQUEST (increment 3, P6).
  - `interGroupId` / `anyUeInd` subscriptions.
  - Roaming (H-PCF to V-PCF).
  - The other `serviceParamData` families (BRID, DDAA, C2, GBDAAA).
  - The A2X N2 PC5 policy (already deferred by ADR-0551).
- **Testing.**
  - `tests/integration/test_pcf_ue_policy_delivery.cpp` runs the real PCF against mock UDM, UDR and AMF processes. Its expected NAS bytes are built independently of the PCF's encoder. The codec has no separate unit test.
  - At the time of the PR, the test had only been syntax-checked locally, because the CI runner shares the host. CI is the first real run.

**Rejected alternatives.**

- **Relying on the `supis` query alone to filter the UDR answer.** Rejected: the PCF would trust the UDR to filter, and one wrong filter would send another UE's parameters to this UE.
- **One UPSC per policy part.** Rejected for this increment: it needs UPSC bookkeeping across deliveries and PLMNs that nothing yet consumes.
- **Re-subscribing on every delivery.** Rejected: subscriptions would pile up at the UDR and AMF. The PCF subscribes once per association and re-delivers on notification.
- **Synchronous delivery on the SBI thread inside the Create handler.** Rejected: the Create answer would wait on three outbound NFs. TS 29.525 does not require the policy to be delivered before the Create response.
