## ADR-0488: LI increment 7d -- the CHF IRI-POI (TS 33.127 7.22, TS 33.128 7.19): plan, and the roamerInOut schema contradiction

**Date:** 2026-10-14. **Status:** Accepted -- option B (user decision, 2026-10-14); Nchf and Rf built; Translated not built; withheld records counted and alarmed (2026-10-20, issue #10; one open question, below).

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

**Decision on Translated (user, 2026-10-14):** leave it unbuilt and move on. A task asking for
TranslatedChargingDataInformation keeps getting no record and a logged error.

**Review: why roamerInOut is withheld rather than derived (2026-10-20, issue #10).** Raised by
Bernard Mwagiru in a public LinkedIn discussion of this ADR (18 Oct 2026). A fourth option was
always available and is now recorded as rejected:
- **D. Derive roamerInOut in the CHF.** The serving PLMN is in the charging data (e.g. the PLMN of
  the TAI in `userLocationinfo`) and the home PLMN follows from the SUPI, so the CHF could compute
  in-bound / out-bound itself. **Rejected:** the ChargingDataEvent is an evidentiary record, and a
  derived roamerInOut would be the CHF's inference presented as what the network reported. The
  j70 ASN.1 has no way to mark a value as derived (`RoamerInOut` is a bare two-value ENUMERATED and
  `NchfChargingDataRequest` has no member that could say where it came from), so the LEA could
  not tell an inference from a reported fact. It would also have nothing truthful to put in the
  record of a subscriber in its home network, the case already ruled out above. This supports B.

**Withheld records counted and alarmed (2026-10-20, issue #10).** Also from that review: under B, a
withheld ChargingDataEvent was a log line and nothing else (a warning for roamerInOut, an error for
a Translated task). No counter and no alarm, so at the LEMF a withheld record looked exactly like a
target with no chargeable events. In LI a missing record is a failure that otherwise surfaces only
when the authority asks for evidence that was never delivered. Built:
- `ChfLiPoi::records_withheld(WithheldReason)` (`nfs/chf/src/li_poi.*`): per-reason atomic tallies,
  bumped at the exact points where a record is withheld, next to the existing log lines, which are
  unchanged. Nchf: no roamerInOut, or a value with no ASN.1 counterpart (the TS 32.291 enum is
  extensible) -> `MissingRoamerInOut`; a Translated task (Nchf and Rf) -> `TranslatedNotBuilt`. One
  count per (request, task), the same unit as one `emit()`. A request that matches no task, or
  only tasks without the ChargingDataEvents extension, owes no record and counts nothing, so the
  counter moves only for an active task.
- `chf_li_poi_records_withheld_total{reason="missing_mandatory_field",field="roamerInOut"}` and
  `{reason="translated_not_built"}`, the label shape proposed in issue #10. It is an OpenTelemetry
  observable counter over those tallies, registered in `nfs/chf/src/main.cpp`. `field` is the
  ASN.1 member name as TS 33.128 spells it.
- Alert `ChfLiPoiRecordsWithheld` (`deploy/prometheus/business_alerts.yml`, new group
  `lawful-interception-alarms`): `increase(...[15m]) > 0`, `severity: critical`,
  `domain: lawful-interception`. Expect it to fire whenever an intercepted home-network subscriber
  generates charging traffic, because nfs/smf never sends roamerInOut. That is accurate: under B
  those records really are not being delivered.

Choices and rejected alternatives for the counter:
- *A synchronous `Counter::Add` with labels* (the UDSF/SMF pattern). Rejected: a labelled series
  then appears only on its first increment, already at 1, and Prometheus's `increase()` has no
  earlier sample to compare it with. The alert would miss the first withheld record of every CHF
  process, and in LI the first missing record is the one that matters. The observable counter
  exports both series at 0 from start-up. It also keeps the tally in `ChfLiPoi`, where a test can
  read it without an OTel SDK. It is an observable *counter* and not an observable gauge (the
  `chf_rating_db_pool_exhaustion_total` precedent), so the exported TYPE is counter and the
  `_total` suffix is accurate.
- *Register the metric only when `li_poi.enabled`*. Rejected: then the metric's mere presence on a
  CHF's `/metrics` would show which CHFs carry a POI. It is registered on every CHF (0 when the
  POI is off).
- *A task XID, LIID or target label*. Rejected: that would copy warrant identifiers into general
  monitoring. The `chf-li-poi` log lines already name the task for whoever investigates.
- Not counted (disclosed, outside issue #10): a ChargingDataEvent that fails to encode (logged,
  `could not encode ChargingDataEvent`) and an LI_X2 delivery failure inside the POI runtime's
  `emit()`. Both are also records that never arrive, but they are failures rather than
  withholding by design, and they would need their own reasons. The NWDAF POI (ADR-0489) also
  withholds records (no consumer NF type) and has no counter either.

**Open point (question to the user): this metric can reveal that interception is happening.** TS
33.126 j30 R6.6-30 requires that non-authorised personnel or processes that are part of the service,
automated systems included, cannot detect that interception is taking place. R6.6-80 limits
knowledge of the operational use of interception capabilities to authorised parties. The counter is
on the CHF's ordinary `/metrics` endpoint, so anyone or anything that scrapes it sees it rise when an
intercepted target's charging request is withheld. From that they learn that this CHF has an active
task and when its target is active. Mitigated here: no task or target label, the metric is present
on every CHF, and the alert's `domain: lawful-interception` label lets Alertmanager route it only
to LI-authorised receivers. **Not mitigated: the scrape endpoint itself is not LI-restricted.**
Alternatives, none built: (a) a separate metrics listener for LI metrics, scraped only from the LI
domain; (b) report the withholding to the ADMF with X1 `ReportTaskIssue` (ETSI TS 103 221-1,
`TS_103_221_01.xsd`; `nfs/li-admf` already parses it, ADR-0462), which is the spec's own NE-to-ADMF
channel, but the POI runtime has no X1 client toward the ADMF yet (`poi_runtime.cpp` logs that its
own ReportNEIssue "needs an X1 client"). Before production, decide whether to keep the CHF-side
metric as built, move it behind (a), or replace it with (b).
