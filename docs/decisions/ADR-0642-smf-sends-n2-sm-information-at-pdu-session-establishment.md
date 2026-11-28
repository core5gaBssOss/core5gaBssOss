## ADR-0642: SMF sends N2 SM information at PDU session establishment, and the downlink PDR matches the UE IP and carries the QFI

**Date:** 2026-11-27. **Status:** Proposed.

**Context.** v0.3.0 item 1 (ADR-0572) is downlink user plane. Tracing it before touching the UPF
found that the N3 user plane cannot come up at all in a normal PDU Session Establishment, for
reasons upstream of the UPF:

1. TS 23.502 V19.9.0 4.3.2.2.1 step 11: the SMF's `Namf_Communication_N1N2MessageTransfer`
   carries "N2 SM information (PDU Session ID, QFI(s), QoS Profile(s), CN Tunnel Info, S-NSSAI,
   Session-AMBR, PDU Session Type, ...)" next to the N1 SM container. The SMF
   (`nfs/smf/src/main.cpp`, CreateSMContext) sent only the N1 SM container (the NAS Accept). The
   gNB was therefore never told the UPF's uplink F-TEID or the QoS flow, never answered with its
   downlink F-TEID, and the SMF's PDU_RES_SETUP_RSP branch (ADR-0259), which installs the
   downlink FAR, ran only in tests.
2. The downlink PDR that branch installs (`install_downlink_far`, ADR-0092) matched
   `SourceInterface=Core` only. TS 29.244 V19.6.0 8.2.62 names the UE in a downlink PDI as a
   destination address (UE IP Address with S/D = 1); ADR-0525 already allocates the UE's IPv4
   address and puts it (S/D = 0) in the uplink PDI.
3. No QFI reached the UPF. TS 29.244 5.4.2: "For 5GC, the mapping of DL traffic to QoS flows is
   achieved by configuring QERs with QFI(s) for the QoS flow marking and associating FARs to the
   downlink PDRs"; the gNB expects that QFI in the PDU Session Container of each downlink G-PDU
   (TS 38.415 V19.1.0 5.5.2.1).
4. The N2 handover transfer (HANDOVER_REQUIRED, ADR-0249) hard-coded QFI 1 / 5QI 9, while the
   NAS Accept's QoS rule used QFI = 5QI & 0x3F from PCF's `authDefQos`. For any 5QI other than 1
   the gNB and the UE were told different QFIs. It also left out the PDU Session Aggregate Maximum
   Bit Rate, which TS 38.413 V19.4.0 9.3.4.1 says "shall be present when at least one Non-GBR QoS
   flow is being setup".

The AMF side (NGAP PDU Session Resource Setup Request / Response, steps 12-15) is also missing;
it is the next PR (one NF per turn), and the UPF downlink datapath is the one after that. The
user approved this order (SMF, then AMF, then UPF) on 2026-11-27.

Specs added locally for this work (not committed, ADR-0507; `specs/3gpp/MANIFEST.tsv` records the
versions): TS 29.281 V19.2.0 (GTP-U), TS 38.415 V19.1.0 (PDU Session Container), TS 38.413
V19.4.0 (NGAP).

**Decision:**

- **One QoS flow decision.** `default_qos_flow_from(SmPolicyDecision)` reads PCF's first session
  rule once, before N4 Session Establishment: 5QI, QFI (5QI & 0x3F, as before), ARP priority
  level, pre-emption capability and vulnerability. Defaults when absent: 5QI 9, ARP 8, shall not
  trigger pre-emption, not pre-emptable (the values the handover path used). The NAS Accept, the
  N2 transfer, the session QER and the handover transfer all use it. It is stored on the SM
  context as `qosFlow` (`qfi`, `fiveQi`, `arpPriorityLevel`, `arpMayPreempt`, `arpPreemptable`,
  and `ambrUlBps`/`ambrDlBps` when the Session-AMBR parses).
- **Step 11.** `nfs/smf/src/n2_sm_info.{hpp,cpp}` builds the PDU Session Resource Setup Request
  Transfer (TS 38.413 9.3.4.1): PDU Session Aggregate Maximum Bit Rate (ID 130, in bit/s, from
  the same Session-AMBR strings as the NAS Accept), UL NG-U UP TNL Information (ID 139, the UPF's
  N3 F-TEID from N4 Session Establishment), PDU Session Type (ID 134, IPv4) and the QoS Flow Setup
  Request List (ID 136, one item: QFI, non-dynamic 5QI, ARP). The N1N2MessageTransfer gets
  `n2InfoContainer` (`TS29518_Namf_Communication.yaml` `N2InfoContainer`: `n2InformationClass`
  SM, `smInfo.pduSessionId`, `smInfo.sNssai`, `smInfo.n2InfoContent.ngapIeType`
  PDU_RES_SETUP_REQ, `ngapData.contentId` "n2SmInfo") and a third multipart part
  (`application/vnd.3gpp.ngap`). The handover branch now calls the same builder with the stored
  `qosFlow`.
- **Downlink PDR.** `nfs/smf/src/downlink_rule.{hpp,cpp}` (moved out of `install_downlink_far` so
  it can be unit-tested) adds UE IP Address with S/D = 1 to the downlink PDI and a QER ID to the
  downlink PDR.
- **One QER per session, carrying the QFI.** The UPF datapath holds one QER per TEID
  (`Datapath::register_qer`, ADR-0071); a Create QER in a Session Modification replaces it. A
  separate QFI-only QER would therefore have wiped the Session-AMBR QER (ADR-0308) and silently
  removed the uplink throttle. Instead the session QER (`kQerId` = 1) carries the QFI IE (TS
  29.244 8.2.89, type 124, `pfcp_core::encode_qfi`/`decode_qfi`, #94) from establishment on, and
  the downlink PDR references QER 1. When establishment installed no QER (PCF's AMBR did not
  parse), the downlink Session Modification creates QER 1 with gate OPEN/OPEN, the QFI and no MBR,
  and the SM context's `qerInstalled` becomes true, so a later re-authorisation sends Update QER
  (ADR-0328).
- **Without an N4 session** (no UPF association yet), the N1N2MessageTransfer still goes out with
  the N1 SM container only, logged as a warning: UL NG-U UP TNL Information is mandatory, and
  there is no F-TEID to put in it.

**Tests:**

- `conformance_tests`: `PfcpQfi.EncodesTheSixBitValueAndIgnoresSpareBitsOnDecode` (in #94, the
  pfcp-core PR split out of this one so each PR touches one area);
  `SmfDownlinkRule.PdrMatchesTheUeAsDestinationAndReferencesTheSessionQer`,
  `SmfDownlinkRule.CreatesTheSessionQerWithTheQfiWhenEstablishmentDidNot`,
  `SmfDownlinkRule.WithoutAUeAddressThePdiMatchesSourceInterfaceOnly`.
- `integration_tests`: `SmfN2SetupTransfer.EncodesTheMandatoryIesAndTheGivenQosFlow` (decode of
  the builder's bytes) and
  `SmfN2SetupTransfer.EstablishmentN1N2CarriesN2SmInfoMatchingTheHandoverTransfer` (real NRF, UPF,
  SMF and PCF; in-process AMF): the N1N2MessageTransfer carries `n2InfoContainer` and an NGAP part
  that decodes, with a non-zero UL F-TEID, and the later HANDOVER_REQUIRED transfer has the same
  F-TEID, QFI, 5QI, ARP and Session-AMBR.

**Rejected alternatives.**

- *Let the UPF take the UE IP from the uplink PDR and use a fixed QFI.* One PR instead of three,
  but the UPF would ignore what the SMF provisioned and mark every flow with the same QFI. The user
  chose the conformant path.
- *A second QER, only for the QFI.* What TS 29.244 allows and what a multi-flow session will need,
  but with this UPF's one-QER-per-TEID datapath it replaces the Session-AMBR QER. Revisit when the
  UPF holds QERs per QER ID (the UPF downlink PR or later).
- *Build the transfer in the AMF.* TS 38.413 9.3.4.1: "This IE is transparent to the AMF." The SMF
  owns the content.

**Disclosed.**

- The AMF does not yet send NGAP PDU Session Resource Setup Request (TS 23.502 4.3.2.2.1 step 12)
  or forward the Response (steps 14-15); until the AMF PR lands it ignores `n2InfoContainer`, so
  nothing changes on the air interface yet.
- QFI = 5QI & 0x3F is unchanged. TS 23.501 lets the SMF assign any QFI; this build has only the
  default QoS flow.
- Every downlink install still sends Create PDR 2 / Create FAR 2, also on a path switch, where
  Update FAR is the precise message (unchanged from ADR-0092/ADR-0259). The UPF PR has to treat a
  repeated Create of the same rule id as a replacement.
- The UPF still only parses and logs the downlink PDR/FAR; no downlink packet flows until the UPF
  PR. The UPF's establishment path ignores the QFI IE in Create QER (it is not read), which is
  harmless today.
- PDU Session Type is always IPv4 (ADR-0525 allocates IPv4 only).
