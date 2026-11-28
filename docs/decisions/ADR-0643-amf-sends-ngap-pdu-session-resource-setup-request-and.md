## ADR-0643: AMF sends NGAP PDU Session Resource Setup Request and forwards the Response to the SMF

**Date:** 2026-11-28. **Status:** Proposed.

**Context.** v0.3.0 item 1 (ADR-0572), second of three PRs (SMF ADR-0642, then AMF, then UPF;
order approved by the user on 2026-11-27, this PR's plan on 2026-11-28). Since ADR-0642 the SMF's
N1N2MessageTransfer carries N2 SM information (the PDU Session Resource Setup Request Transfer)
whenever it has an N4 session. The AMF ignored it and always sent the Accept in a
DownlinkNASTransport (ADR-0038), so the gNB never learnt the UPF's uplink F-TEID, never answered
with its downlink F-TEID, and the SMF's PDU_RES_SETUP_RSP branch (ADR-0259) was never reached
outside tests.

TS 23.502 V19.9.0 4.3.2.2.1:

- Step 12: "AMF to (R)AN: N2 PDU Session Request (N2 SM information, NAS message (PDU Session ID,
  N1 SM container (PDU Session Establishment Accept)), ...). If the N2 SM information is not
  included in the step 11, an N2 Downlink NAS Transport message is used instead."
- Step 14: the N2 PDU Session Response carries N2 SM information with the AN Tunnel Info.
- Step 15: "AMF to SMF: Nsmf_PDUSession_UpdateSMContext Request (SM Context ID, N2 SM information,
  Request Type). The AMF forwards the N2 SM information received from (R)AN to the SMF."

TS 38.413 V19.4.0 8.2.1.2 says, for the response: "the AMF shall, for each PDU session indicated
in the PDU Session ID IE, transfer transparently the PDU Session Resource Setup Response Transfer
IE or PDU Session Resource Setup Unsuccessful Transfer IE to the SMF associated with the concerned
PDU session."

Identifiers, read from `specs/NGAP/ngap-17.9.asn`:

- Procedure: `id-PDUSessionResourceSetup ProcedureCode ::= 29`, CRITICALITY reject (line 700).
- Request IEs: AMF-UE-NGAP-ID 10 and RAN-UE-NGAP-ID 85 (mandatory),
  PDUSessionResourceSetupListSUReq 74 (mandatory).
- Response IEs: PDUSessionResourceSetupListSURes 75 and
  PDUSessionResourceFailedToSetupListSURes 58 (both optional).
- List item `PDUSessionResourceSetupItemSUReq` (line 9070): pDUSessionID, pDUSessionNAS-PDU
  (OPTIONAL), s-NSSAI, pDUSessionResourceSetupRequestTransfer.

**Decision:**

- **ASN.1.** `PDUSessionResourceSetupRequest` and `PDUSessionResourceSetupResponse` use
  `ConcreteProtocolIE-Container`, the ADR-0031 patch. Per X.691 10.9 this changes no wire
  encoding.
- **Codec.** `nfs/amf/src/pdu_session_resource_setup.{hpp,cpp}` encodes the Request and decodes the
  Response. Both transfers are opaque bytes; the AMF builds only the message around them.
- **Step 12.**
  - The N1N2MessageTransfer route sends a PDU SESSION RESOURCE SETUP REQUEST when
    `n2InfoContainer` is class SM with `smInfo.n2InfoContent` of `ngapIeType` PDU_RES_SETUP_REQ
    (or no `ngapIeType`) and its `ngapData` part is present.
  - The request has one list item: the PDU session id, `smInfo.sNssai` as S-NSSAI, the SMF's
    transfer unchanged, and as PDU Session NAS-PDU the same secured DL NAS TRANSPORT
    `send_dl_nas_transport` builds. It uses the same NAS COUNT, so the UE's view does not change.
  - Without N2 SM information the route still sends a DownlinkNASTransport, as step 12 says.
  - The SMF's own `smInfo.sNssai` is the S-NSSAI. A missing or malformed one is answered with 400
    MANDATORY_IE_MISSING, `invalidParams` `/n2InfoContainer/smInfo/sNssai`, rather than sent with
    a slice this AMF invented. A referenced N2 part that is absent is answered with 400
    INVALID_MSG_FORMAT, like a missing N1 part.
- **Steps 14-15.**
  - A successfulOutcome of procedure 29 on the UE's association is decoded. Its AMF-UE-NGAP-ID
    must match the association's UE; otherwise it is logged and dropped.
  - Each Setup Response item is POSTed to `{smf}/nsmf-pdusession/v1/sm-contexts/{ref}/modify` as
    multipart/related: SmContextUpdateData `n2SmInfoType` PDU_RES_SETUP_RSP, `n2SmInfo.contentId`
    "n2SmInfo", and an `application/vnd.3gpp.ngap` part with the transfer. Failed-to-Setup items
    are sent the same way with PDU_RES_SETUP_FAIL.
  - `{ref}` is the smContextRef stored since ADR-0249. The SMF's status is logged.

**Tests:**

- `integration_tests` `AmfPduSessionResourceSetup.*` (no NF running): the AMF's encoder against
  the test gNB's independent parser, and the test gNB's Response builder against the AMF's decoder.
  They cover the IDs, S-NSSAI with and without SD, the NAS-PDU, transfers byte for byte, the
  rejection of non-Response PDUs and `parse_sd`.
- `AmfNgapTestGnb.PduSessionResourceSetupCarriesTheSmfTransferAndTheGnbTunnelReachesTheUpf` runs
  real NRF, UDR, UDM, AUSF, PCF, UPF, SMF and AMF with the test gNB over SCTP. The test:
  1. Registers the UE and establishes a PDU session.
  2. Checks that the AMF sends PDUSessionResourceSetupRequest: IDs, PDU session 5, the requested
     S-NSSAI, and a transfer that decodes with a non-zero UPF F-TEID. The NAS-PDU deciphers and
     MAC-verifies to the SMF's Accept (shared `establish_pdu_session`).
  3. Answers with gNB TEID 0x0A0B0C0D at 127.0.0.9.
  4. Asserts the AMF's log line for a 2xx from the SMF, and the UPF's own log of a Session
     Modification whose Outer Header Creation carries that TEID and address.
- **Test gNB.** `tests/integration/ngap_test_gnb` learns the Request, a Response builder and a
  Request Transfer parser. `extract_downlink_nas` also reads the Request's PDU Session NAS-PDU,
  because every existing test that spawns a UPF now receives a Setup Request instead of a
  DownlinkNASTransport.

**Rejected alternatives.**

- *A stand-in SMF in the test.* The test would not depend on ADR-0642, but NRF refuses an
  `nsmf-pdusession` token unless a registered SMF offers it (`nfs/nrf/src/token_authz.cpp`), and
  it would not show the tunnel reaching the UPF. This PR is stacked on #93 instead.
- *Build the S-NSSAI from the UE's own request in the AMF.* That would duplicate what the SMF
  already put in `smInfo.sNssai`, and the two could disagree.
- *Send the NAS PDU as the message-level NAS-PDU IE (38).* 8.2.1.2 has the gNB pass that IE to
  the UE unconditionally ("If the NAS-PDU IE is included ... the NG-RAN node shall pass it to the
  UE"), but "shall not send to the UE the PDU Session NAS PDUs associated to the failed PDU
  sessions". The per-item IE is the one that keeps an Accept from reaching the UE for a session the
  gNB could not set up.

**Disclosed.**

- No UE Aggregate Maximum Bit Rate in the Request. 8.2.1.2 says it "should be sent ... if the AMF
  has not sent it previously". This AMF tracks no subscribed UE-AMBR (the handover path uses a
  fixed lab default, `nfs/amf/src/ngap_handover.cpp`), and that default is not copied here.
- One PDU session per Request. No RAN Paging Priority, UE Slice MBR or CN assisted RAN parameters.
- User Location Information in the Response is not forwarded.
- On PDU_RES_SETUP_FAIL, step 15 requires a PDU Session Establishment Reject in the SMF's answer.
  This SMF only decodes and acknowledges (ADR-0260), so nothing is relayed back to the UE.
- No timer on the Response. A gNB that never answers leaves the session without a downlink rule.
- The AMF matches the Response to the one UE of its association (ADR-0031 scope). It does not look
  up the AMF-UE-NGAP-ID across associations.
- Other N2 SM information in an N1N2MessageTransfer (for example PDU_RES_MOD_REQ, PDU_RES_REL_CMD)
  still goes out as N1 only.
