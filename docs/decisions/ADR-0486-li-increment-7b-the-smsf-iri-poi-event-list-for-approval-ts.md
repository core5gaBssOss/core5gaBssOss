## ADR-0486: LI increment 7b -- the SMSF IRI-POI: event list for approval (TS 33.127 6.2.5, TS 33.128 6.2.5.3)

**Date:** 2026-10-13. **Status:** Proposed -- option 1 chosen, blocked on missing specs (TS 24.011, TS 23.040, TS 29.002); no code written.

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
