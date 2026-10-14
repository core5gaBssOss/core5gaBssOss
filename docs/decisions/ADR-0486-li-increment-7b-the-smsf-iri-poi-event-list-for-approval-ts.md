## ADR-0486: LI increment 7b -- the SMSF IRI-POI: event list for approval (TS 33.127 6.2.5, TS 33.128 6.2.5.3)

**Date:** 2026-10-13. **Status:** Accepted -- option 1; increments 1-4 built (MO and MT SMS over NAS, SBI path). MAP MT-FORWARD-SHORT-MESSAGE not built.

**Context.** The LI plan (ADR-0364) step 7 continues with the SMSF, next after the UDM (ADR-0475).
TS 33.128 6.2.5.2/6.2.5.3 names exactly two xIRI record types the SMSF's IRI-POI generates:
`SMSMessage` (an SMS actually being transferred) and `SMSReport` (the delivery report/ack for a
previously-intercepted SMS). Target identifiers: SUPI (IMSI/NAI), PEI (IMEI/IMEISV), GPSI
(MSISDN/NAI) -- same shape the SMF/UDM POIs already match on.

**Trigger events, per 6.2.5.3, against what `nfs/smsf` implements.** The spec names: the SMSF
receiving an SMCP `CP-DATA_RP-DATA` message *via AMF* (uplink/SMS-MO) or sending an SMCP
`CP-DATA_RP-ACK`/`RP-ERROR` (the report, also via AMF); and the SMSF receiving/responding to a
TCAP MAP `MT-FORWARD-SHORT-MESSAGE` (downlink/SMS-MT, legacy SS7 interworking).

- **Uplink: the hook point exists.** `nfs/smsf`'s `SendSMS` (`/sendsms`, body `SmsRecordData`) IS
  the stage-3 form of stage-2 `Nsmsf_SMService_UplinkSMS` -- `TS29540_Nsmsf_SMService.yaml`'s own
  schema descriptions say `SmsRecordData` is "the information sent within a request message of the
  UplinkSMS service operation" and `SmsRecordDeliveryData` the "result of invoking the UplinkSMS
  service operation". (An earlier draft of this ADR wrongly called `SendSMS` a different NF-to-NF
  forwarding operation and said UplinkSMS was unimplemented; corrected here.) What is missing is a
  live caller: `nfs/smsf/src/main.cpp`'s header (ADR-0184) discloses that no NF calls `SendSMS`
  -- the AMF does not relay uplink NAS SMS to the SMSF.
- **Mandatory `SMSMessage` fields need SMS payload decoding.** `terminatingSMSParty` (TP-DA),
  `messageType`, `rPMessageReference` and `sMSTPDUData` come from inside the CP/RP/TPDU layers, which
  needs TS 24.011 (CP/RP) and TS 23.040 (TPDU). Neither spec is in `specs/3gpp/`.
- **Downlink:** MAP `MT-FORWARD-SHORT-MESSAGE` needs TS 29.002, not in hand and not in
  `libs/map-core`.
- **`SMSReport`:** needs RP-ACK/RP-ERROR generation plus an `N1N2MessageTransfer` back to the AMF.

**Options presented to the user:**

1. **Build the missing SMSF wiring first** (AMF uplink-SMS relay, CP/RP/TPDU decoding, then the
   LI-POI hooks on `SendSMS`) -- conformant, larger scope than the UDM POI; blocked until TS 24.011,
   TS 23.040 (and TS 29.002 for downlink) are in hand.
2. **Hook the LI-POI onto `SendSMS` now, without payload decoding** -- not viable: `SMSMessage`'s
   mandatory fields cannot be filled without the specs above, and inventing them is out.
3. **Defer the SMSF and move to the next step-7 item** (NEF 7.9, CHF 7.22, NWDAF 7.18, NRF SIRF).

**Decision:** the user chose option 1 (on the earlier, incorrect premise; re-confirmation asked
after the correction). Blocked on obtaining TS 24.011 / TS 23.040 / TS 29.002 -- per CLAUDE.md,
stop and ask rather than decode from memory.

**Rejected alternatives.** Building events by inventing a trigger not in the spec and not in this
project's real SMSF operations: would be fabricating a call flow, the project's own worst failure
mode. Silently choosing option 2 without disclosure: would repeat the exact "nothing calls it yet"
gap class this project already had to come back and close once (ADR-0463/0464 -> ADR-0484), not
disclosed up front this time.

**Update 2026-10-14 -- specs in hand, user directive "proceed with SMSF, don't wait for my approval".**
TS 24.011 j00, TS 23.040 j00, TS 29.002 j10, TS 24.007 j50, TS 23.502 j90 and TS 24.501 j80 were
fetched with `tools/specs/fetch_3gpp_specs.py` (official 3GPP archive) and committed to `specs/3gpp/`.
Procedure list being built (TS 23.502 clause 4.13.3.3 MO SMS over NAS, TS 33.128 clause 6.2.5.3):

1. **Increment 1 (done):** `libs/sms-core` -- CP layer (TS 24.011 8.1, PD/TI per TS 24.007
   11.2.3.1), RP layer (TS 24.011 8.2: RP-DATA/ACK/ERROR/SMMA), TP layer as far as the SMSF needs
   (TS 23.040 9.2.3.1 TP-MTI, TP-MR, 9.1.2.5 TP-DA for SMS-SUBMIT and SMS-COMMAND), SMS-SUBMIT-REPORT
   for RP-ACK/RP-ERROR (9.2.2.2a) with TP-SCTS (9.2.3.11). `li_core::xiri` gains `SmsMessage` [12]
   and `SmsReport` [56]. Tests: 10 `Sms*` codec tests (one against the widely published SMS-SUBMIT
   example PDU, not only self round-trips) + 2 xIRI round-trips. Disclosed: extended TI (TIO=7)
   rejected; SMS-DELIVER/STATUS-REPORT TP-OA/TP-RA not extracted (MT path); SMSMessage.peerNFAddress
   and the truncatedSMSTPDU/3GPP2 SMSTPDUData choices not modelled.
2. **Increment 2:** SMSF -- `SendSMS` (= `Nsmsf_SMService_UplinkSMS`) decodes CP-DATA/RP-DATA/TPDU;
   returns CP-ACK (step 2c) and, since this project has no SMS-IWMSC, an RP-ERROR carrying an
   SMS-SUBMIT-REPORT (steps 6a-6b) via `Namf_Communication_N1N2MessageTransfer`; IRI-POI emits
   `SMSMessage` on the RP-DATA (linkTransferStatus = transferFailed per table 6.2.5.3-1 NOTE 5) and
   `SMSReport` on the RP-ERROR.
3. **Increment 3:** AMF -- UL NAS TRANSPORT payload container type SMS -> `UplinkSMS`; N1N2
   n1MessageClass SMS -> DL NAS TRANSPORT; `Nsmsf_SMService_Activate` + "SMS allowed" in
   Registration Accept (TS 23.502 clause 4.13.3.1).

**Increment 2 (done, 2026-10-14).** `nfs/smsf`: `SendSMS` decodes CP/RP/TPDU (`sms_procedures.cpp`),
answers CP-ACK then CP-DATA/RP-ERROR [SMS-SUBMIT-REPORT] (`lastMsgIndication` on the latter) through
an ordered single-worker `N1n2Sender` to `amf_base_url`; RP-Cause/TP-FCS from `config/smsf.json`
`mo_sms` (#38, 0xC2). IRI-POI (`li_poi.cpp`): SMSMessage (fromTarget, transferFailed per NOTE 5,
terminatingSMSParty.sMSAddress = TP-DA field octets, location from the request's ueLocation) and,
once the AMF accepted the RP reply, SMSReport under the same tasks. TruncateTPUserData: the X1 codec
now parses/serialises `X1Extensions/SMSFExtensions/TruncateTPUserData` (it was schema-valid but
silently ignored before -- a task forbidding content would have received full TP-UD), the POI
runtime carries it per task, and `sms_core::redact_tpdu` implements TS 33.128 7.4.5.2.1 (TS 23.038
encodings; non-"SMS Control" UDH IEs -> Filler); if a TPDU cannot be redacted the SMSMessage is
withheld, never sent unredacted. Disclosed interpretations: "Space" for 8-bit data = 0x20; a
compressed TP-UD is zeroed. Verified: 5 `SmsfMoSms` unit tests, 3 `LiSmsfEndToEnd` tests through
the real smsf + nrf processes (target, TruncateTPUserData, non-target), full conformance suite (513).
Also fixed: the UDM POI's default LI_X1 port 7811 (ADR-0475) collided with li-mdf3's X1 port; the
UDM POI now defaults to 7813 and the SMSF POI to 7814.

**Increment 3a (done, 2026-10-14).** AMF Registration supporting SMS over NAS (TS 23.502 4.13.3.1):
`decode_registration_request` reads the 5GS update type "SMS requested" bit (IEI 0x53, octet 3 bit
1); its optional-IE walk now continues past the UE security capability and honours the three formats
TS 24.501 8.2.6.1 uses (Type-1 0x8-..0xC-, TV-7 0x52, TLV-E 0x7-), so a UE sending any of them no
longer desyncs it. When requested, `activate_sms_at_smsf` calls Nsmsf_SMService_Activate (PUT
ue-contexts/{supi}; supi, amfId, accessType) BEFORE RegistrationAccept, and the Accept's SMS allowed
bit (9.11.3.6 octet 3 bit 4) is set only on 201/204 -- a failed activation gives "not allowed" and the
registration proceeds (TS 24.501 5.5.1.2.4). Disclosed: the SMSF is `config/amf.json`
`smsf_base_url`, not TS 23.501 6.3.10 selection; step 3's SMS Subscription data check (Nudm_SDM_Get
sms-data) is not made; step 2's UDM "UE context in SMSF data" is not read. Verified: 2 new
`NasCodec` tests, `AmfSmsRegistration` (2 tests, real AMF over NGAP/NAS + real SMSF/AUSF/UDM/UDR/NRF;
the Accept is MAC-verified/deciphered with UE-side keys and the SMSF context checked on the SMSF).
Remaining, increment 3b: UL NAS TRANSPORT payload container type SMS -> UplinkSMS, and the AMF's
n1-n2-messages route accepting n1MessageClass SMS -> DL NAS TRANSPORT.

**Increment 3b (done, 2026-10-14).** AMF relay for MO SMS over NAS (TS 23.502 4.13.3.3): an UL NAS
TRANSPORT with payload container type SMS (TS 24.501 9.11.3.40) is forwarded to the SMSF as
Nsmsf_SMService_UplinkSMS (`try_relay_uplink_sms`, tried before the PDU-session and deregistration
handlers in their phases, no phase change); the AMF's n1-n2-messages route turns n1MessageClass SMS
into a secured DL NAS TRANSPORT (SMS) via `NgapUeRegistry::send_dl_nas_sms`. The post-registration
uplink COUNT is now tracked in `next_uplink_count` from RegistrationComplete on (it was a fixed 2 for
the PDU Session Establishment Request), so any number of SMS may precede it. An existing conformance
test that used payload type SMS as its "unsupported type" example now uses LPP (3) -- SMS is
supported by design. Disclosed: SMS is relayed only for a UE that got "SMS allowed"; UplinkSMS
carries no gpsi/pei/ueLocation/ueTimeZone yet (step 2b's IMEISV, ULI, time zone); lastMsgIndication
is accepted with no AMF behaviour (step 6a NOTE: implementation specific); the MT path (4.13.3.6-8)
and SMS over non-3GPP access are not built. Pre-existing, not changed here: the persisted
UeSecurityContextStore uplink COUNT is not advanced by post-registration uplinks, so a ServiceRequest
after idle relies on its own count handling (same as after a PDU session). Verified:
`AmfSmsRegistration.MoSmsRoundTripsThroughAmfAndSmsf` -- a test UE's CP-DATA/RP-DATA/SMS-SUBMIT goes
through the real AMF to the real SMSF, and the CP-ACK (DL COUNT 2) and CP-DATA/RP-ERROR (DL COUNT 3,
same RP-MR) come back MAC-verified and deciphered with the UE's own keys.

**Increment 4 (2026-10-14) -- MT SMS over NAS, SBI path.** TS 23.540 (j20), TS 29.540 (j60), TS
29.577 (j40) and TS 29.579 (j40) fetched into `specs/3gpp/`. The REL-19 YAML already defines the
MT operation: `SendMtSMS` (`POST .../ue-contexts/{supi}/send-mt-sms`, SmsData / SmsDeliveryData from
TS29577) is the stage-3 form of Nsmsf_SMService_MtForwardSm (TS 23.540 6.4.2.2, consumers SMS-GMSC,
SMS Router, IP-SM-GW). That is the path built; MAP MT-FORWARD-SHORT-MESSAGE (TS 29.002) is NOT --
TS 23.540 5.1.2 step 5 makes the SBI operation the alternative to MAP/Diameter for an SMSF that
supports SMS_SBI, and this SMSF has no SS7/MAP interface.

Procedure (TS 23.502 4.13.3.6 steps 4-7, 4.13.3.7; TS 23.540 5.1.2 / 5.1.5), `nfs/smsf/src/mt_sms.*`:
SendMtSMS validates the payload, calls Namf_MT_EnableUEReachability (step 4), sends the CP-DATA to the
UE via N1N2MessageTransfer (5a), takes the UE's CP-ACK (5c) and CP-DATA(RP-ACK/RP-ERROR) (6a) from
UplinkSMS (an uplink on a network-originated TI -- TI flag 1 -- goes to the MT transaction, not the
MO path), sends the closing CP-ACK with lastMsgIndication (6c), and answers 200 with the delivery
report (step 7). AMF: `ue-reachind` now reports REACHABLE only for a UE with a live NGAP association
(CM-CONNECTED, 4.13.3.7) and UNREACHABLE otherwise; it used to echo the request back.
`AmfMtIntegration.EnableUeReachability404ThenRealAck` became `...404ThenUnreachableWithoutNgap` (its
UE context is SBI-created, with no NGAP).

Disclosed interpretations (no clause states them; open for review):
1. **Payload layer.** TS 29.540 6.1.6.4.2 (same text in 29.577/29.579) only says the vnd.3gpp.sms part
   "may encode e.g. CP-DATA, CP-ACK, CP-ERROR". The request is taken as CP-DATA(RP-DATA network->MS
   [SMS-DELIVER or SMS-STATUS-REPORT]); anything else is 400 SMS_PAYLOAD_ERROR. The RP-DATA (RP-MR,
   RP-OA = SC address, TPDU) is relayed to the UE as received; only the CM-layer TI is the SMSF's own.
   The delivery report is CP-DATA in the reply direction of the request's TI, carrying the UE's
   RP-ACK/RP-ERROR unchanged.
2. **Errors.** No Nsmsf/Nipsmgw/Nrouter application error covers "UE not reachable" or "no delivery
   report" (TS 23.540 5.1.5 step 7 only says "with error cause"), so those are 504 ProblemDetails with
   NO `cause`, never an invented value. A UE RP-ERROR (e.g. #22 memory capacity exceeded) is a
   delivery report and returns 200 with that RP-ERROR -- the GMSC gets the real RP-Cause rather than a
   status code that cannot carry it. CONTEXT_NOT_FOUND / SMS_PAYLOAD_MISSING / SMS_PAYLOAD_ERROR are
   now sent as ProblemDetails.cause; over `mt_sms.max_concurrent` is 503 NF_CONGESTION (TS 29.500).
3. **TR1N.** TS 24.011 clause 10 fixes TR1M (35-45 s) for the MS but no network value; TR1N is
   `config/smsf.json` `mt_sms.tr1n_seconds` (40). TC1* CP-DATA retransmission is not implemented.
4. **One MT transfer per UE at a time** (TS 24.011 3.2, "parallel message transfer in the same
   direction is prohibited"): a second waits for the first within its own TR1N.
5. **LI.** TS 33.128 6.2.5.3 names MAP MT-FORWARD-SHORT-MESSAGE Request/Response as the MT triggers;
   they are taken from MtForwardSm, its SBI equivalent. SMSMessage (toTarget, originating sMSAddress =
   TP-OA / TP-RA octets per NOTE 2, terminating = the target's SUPI/PEI/GPSI, otherMessage from
   TP-MMS per NOTE 6, redactedSMSTPDU for an SMS-DELIVER under TruncateTPUserData) is generated once
   the outcome is known, because NOTE 5's linkTransferStatus is whether the SMSF sends the response
   carrying the SMS-DELIVER-REPORT; a failed attempt still yields one (transferFailed), a 503
   (no attempt) does not. SMSReport (deliverReportAck/Error, the UE's SMS-DELIVER-REPORT, location from
   the carrying UplinkSMS per NOTE 7) is sent with the 200; an RP-ACK without RP-User-data has no
   TPDU and yields no SMSReport (sMSTPDUData is mandatory, never fabricated).

Disclosed limits: the SBI server has no deferred-response API, so each MT SMS in flight holds one
SMSF server thread for up to TR1N (capped by `max_concurrent`, default 4); no paging, so a CM-IDLE UE
fails at step 4; no SMS management subscription data check (step 4's first sentence); the AMF is
`amf_base_url`, not UeSmsContextData.amfId; nothing in the project is an SMS-GMSC, SMS Router or
IP-SM-GW, and no Nudm_UECM_SendRoutingInfoForSM / Nudm_ReportSMDeliveryStatus consumer exists, so
SendMtSMS is driven only by tests; peerNFAddress/peerNFType are not reported (the consumer type is
not signalled); MT over non-3GPP access (4.13.3.8) is not built.

Verified: 2 `SmsTp.Mt*` decoder tests and 7 `SmsfMtSms` tests (payload parsing, TI handling, success,
RP-ERROR, AMF refusal / CP-ERROR / TR1N failures, TI rotation, congestion and per-UE serialisation);
`AmfSmsRegistration.MtSmsRoundTripsThroughSmsfAndAmf` (test as SMS-GMSC; real SMSF + AMF; the UE
deciphers the MT CP-DATA with its own keys, answers, gets the closing CP-ACK, and the GMSC's 200
carries the UE's RP-ACK under its own TI) and `...MtSmsToAUeWithNoNgapAssociationFails` (504, no
cause; 400 SMS_PAYLOAD_ERROR); `LiSmsfEndToEnd.MtSmsToATargetReportsSmsMessageAndSmsReport`.
