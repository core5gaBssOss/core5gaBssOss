## ADR-0488: LI increment 7d -- the CHF IRI-POI (TS 33.127 7.22, TS 33.128 7.19): plan, and the roamerInOut schema contradiction

**Date:** 2026-10-14. **Status:** Accepted -- option B (user decision, 2026-10-14); Nchf and Rf built; Translated not built.

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
   error, disclosed. Rf: this CHF DOES serve Rf ACR/ACA (ADR-0059; an earlier draft of this ADR
   wrongly said it had none) -- the RfChargingDataRequest record (full ACR octets + rfChargingEvent,
   no roamerInOut member) is the next increment.

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

**Decision:** **B, strict ASN.1** (user decision, 2026-10-14). A ChargingDataEvent is generated only
for a Charging Data Request whose service-specific UserInformation carries `roamerInOut`; any other
request for a target is withheld and logged as such. Consequence, stated plainly: with today's SMF
(which never sends roamerInOut) a home-network target produces no CHF xIRIs.

**Rejected alternatives.** Picking A or B silently: either one breaks a normative text (interop
risk, or no interception), which is the user's call, not an implementation detail.

**Built (2026-10-14).** X1 `ChargingDataEvents` parsed/serialised (`TaskDetails::charging_data_events`,
carried by `li_poi::TaskInfo`); `li_core::xiri::ChargingDataEvent` [190] (nchfChargingDataRequest
only; `NchfChargingEvent`, `RoamerInOut`); `nfs/chf/src/li_poi.*` hooked on all six Nchf
Create/Update/Release handlers right after authorisation (`config/chf.json` `li_poi`, disabled by
default, X1 7816). The TS 29.571 UserLocation -> xIRI Location converter moved from the SMSF POI to
`libs/li-poi` (`user_location.hpp`) and is shared. nchfChargingEvent comes from the first
service-specific member present in table 7.19.3.7-1 order; `n2ConnectionChargingInformation(5)` has no
R19 ChargingDataRequest member. roamerInOut is read from that member's `userInformation` only (table
7.19.3.4-1 wording) -- SMSChargingInformation's own top-level roamerInOut is not used (disclosed).

Verified: `LiX1Client.ChargingDataEventsRoundTripThroughTheSchema` (both values through the XSD),
`Xiri.ChargingDataEventRoundTripsConvergedAndOffline` (529 conformance pass); `LiChfEndToEnd` (2,
real chf + nrf, real LI_X1, loopback MDF2): a converged OUT_BOUND request and an offline IN_BOUND
request are reported with the unmodified payload, the right SBIReference, nchfChargingEvent and
location; a request without roamerInOut is withheld (logged); a task without the extension gets
nothing. The test runs the CHF with direct-insert CDRs off and an unreachable event-bus broker (the
`test_chf_diameter_rar.cpp` precedent); it needs the rating PostgreSQL (CI's service, a local
container on 5434 here). Existing CHF suites (`ChfDiameterRar`, `ChfProtocolCeilings`) unchanged.

**Rf built (2026-10-14).** `chf::DiameterServer::set_acr_observer` hands every received
Accounting-Request (whole message as received + decoded AVPs) to the POI before it is processed;
`ChfLiPoi::report_rf_acr` matches User-Name and Service-Information(873, vendor 10415)/Subscription-Id
(END_USER_E164 -> MSISDN/GPSI-MSISDN, END_USER_IMSI -> IMSI/SUPI-IMSI, END_USER_NAI -> SUPI-NAI/
GPSI-NAI; SIP URI unsupported) and emits ChargingDataEvent(rfChargingDataRequest: the ACR octets,
rfChargingEvent from the *-Information AVP present -- codes taken from TS_32.299_j00.txt; M2M-
Information(15) is a oneM2M AVP whose code TS 32.299 does not give, unmapped). Rf has no roamerInOut
member, so nothing is withheld there -- a home-network Rf target IS reported. Location on Rf
(binary 3GPP-User-Location-Info) is not decoded: omitted, disclosed. `li_core::xiri::ChargingDataEvent`
now holds `std::variant<NchfChargingDataRequest, RfChargingDataRequest>`. Verified:
`Xiri.ChargingDataEventRoundTripsConvergedOfflineAndRf`, `LiChfEndToEnd.RfAccountingRequestForATarget
IsReportedWithItsOctets` (real CER/CEA then ACR/ACA against the real chf; the record's octets equal
the ACR byte for byte); `ChfDiameterRar` / `ChfProtocolCeilings` unchanged. TS 32.299 j00 fetched.

**Translated records -- scoped 2026-10-14, blocked (question to the user).** TranslatedChargingData
(table 7.19.3.14-1) = releaseID / versonID / specificationNumber (TS 32.297 6.1.2.2/.3/.5, fetched
j00) + recordData "translated ... using the syntax encoding rules as specified in TS 32.298 clause
5.2". Three things are not settled by the specs in hand:
1. No Rel-19 CHF-CDR ASN.1 in hand: TS 32.298 j60 (fetched) carries no ASN.1 module text; the
   existing encoder (`nfs/chf/src/cdr_asn1.cpp`, ADR-0089) was read from V18.8.0 and encodes 11 of
   the 46 ChargingRecord fields (no service-specific information).
2. specificationNumber: TS 32.297 table 6.1.2.5.1 lists domain TSs (32.255 = 20, 32.274 = 15,
   32.256 = 22, 32.254 = 21, ...), none for "CHF-CDR" as such -- it would have to be chosen from the
   service-specific information present, which the table does not say.
3. Per request vs per record: the trigger is each Charging Data Request; a CHF-CDR is a closed
   record (recordOpeningTime, duration, causeForRecClosing). Translating one request into a
   CHFRecord means either a partial-record-per-request or reporting at CDR closure -- neither is
   stated.
Until answered, a TranslatedChargingDataInformation task still gets no record and a logged error.
