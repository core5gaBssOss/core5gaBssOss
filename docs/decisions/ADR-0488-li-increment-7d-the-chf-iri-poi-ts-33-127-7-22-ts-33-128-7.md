## ADR-0488: LI increment 7d -- the CHF IRI-POI (TS 33.127 7.22, TS 33.128 7.19): plan, and the roamerInOut schema contradiction

**Date:** 2026-10-14. **Status:** Proposed -- blocked on one user decision (below); no code written.

**Context.** Next LI step-7 item (ADR-0364) after the NEF (ADR-0487). TS 33.127 7.22.3 / TS 33.128
7.19.3: one record, `ChargingDataEvent` [190], whenever the charging function receives a Charging
Data Request over Nchf (or Rf) for a target. Unlike the NEF, the trigger is live: `nfs/smf` sends
Nchf_ConvergedCharging Create/Update/Release during PDU sessions, and `nfs/chf` serves both
`nchf-convergedcharging/v3` and `nchf-offlineonlycharging/v1`.

**Planned increment (once unblocked).**
1. X1: `TaskDetailsExtensions/X1Extensions/ChargingDataEvents/ChargingDataEventsGenerated`
   (Default / Translated; urn_3GPP_ns_li_3GPPX1Extensions.xsd, V19.3.0) parsed and serialised;
   carried per task by `libs/li-poi`. Absent -> no records for that task (table 7.19.1-1).
2. `li_core::xiri::ChargingDataEvent` with the `nchfChargingDataRequest` choice:
   `sBIchargingData.fullSBIChargingData` = SBIType (encapsulatedSBIConvergedChargingData or
   ...OfflineChargingData, references exactly as 7.19.3.6 gives them, value = the request body
   unmodified), `nchfChargingEvent` from which service-specific information member is present
   (table 7.19.3.7), `location` from the request's user location when present.
3. `nfs/chf` POI (targets per 7.19.1: SUPI, IMSI, GPSI/MSISDN/E164Number, PEI/IMEI/IMEISV, from
   subscriberIdentifier and the service-specific UserInformation) on all six Create/Update/Release
   handlers. TranslatedChargingDataInformation tasks: not supported in this increment (the
   TranslatedChargingDataInfo structure is large) -- such a task would get NO records and a logged
   error, disclosed; Rf: this CHF has no Rf interface.

**The blocker: TS 33.128 j70 contradicts itself on `roamerInOut`.**
- Table 7.19.3.4-1: `roamerInOut`, cardinality 0..1, C -- "Shall be included when contained in the
  UserInformation".
- `TS33128Payloads.asn` (the j70 attachment in `specs/3gpp/33128-attachments/`):
  `NchfChargingDataRequest ::= SEQUENCE { sBIchargingData [1], nchfChargingEvent [2] ... OPTIONAL,
  roamerInOut [3] RoamerInOut }` -- MANDATORY. The attachment CHANGELOG has no fix.
- `RoamerInOut ::= ENUMERATED { in-bound(1), out-bound(2) }` -- no value for a subscriber in its home
  network, which is every ChargingDataRequest `nfs/smf` sends today.

Options:
- **A.** Encode with a locally patched module where `roamerInOut` is OPTIONAL, omitting it when the
  request does not carry it (follows the table). Risk: a LEMF decoding with the published j70 module
  rejects records that omit a mandatory element.
- **B.** Follow the ASN.1 strictly: generate the record only when the request carries roamerInOut;
  otherwise withhold it and log. Every home-network target goes unreported -- the POI exists but
  intercepts almost nothing.
- **C.** Defer the CHF POI and move to the NWDAF POI (TS 33.128 7.16) meanwhile.

Never acceptable: putting `in-bound` / `out-bound` into a record for a non-roaming subscriber.

**Decision:** pending (asked 2026-10-14).

**Rejected alternatives.** Picking A or B silently: either one breaks a normative text (interop
risk, or no interception), which is the user's call, not an implementation detail.
