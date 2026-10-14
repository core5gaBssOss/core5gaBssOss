## ADR-0489: LI increment 7e -- the NWDAF IRI-POI (TS 33.127 7.18, TS 33.128 7.16)

**Date:** 2026-10-14. **Status:** Accepted -- subscription, notification and analytics-info-query records built; roaming records not applicable (no RoamingAnalytics service).

**Context.** Next LI step-7 item (ADR-0364) after the CHF (ADR-0488). TS 33.127 7.18 / TS 33.128
7.16: an IRI-POI in the NWDAF reporting what NF consumers ask about a target UE's analytics. Target
identities: SUPI only (SUPIIMSI, SUPINAI; table 7.16.1.1-1). The UE-related analytics in scope are
the seven of TS 33.127 7.18.1, i.e. the NWDAFEvent ENUMERATED: serviceExperience, uEMobility,
uEComm, abnormalBehaviour, dispersion, relativeProximity, pDUSessionTraffic (TS 29.520 NwdafEvent
SERVICE_EXPERIENCE, UE_MOBILITY, UE_COMMUNICATION, ABNORMAL_BEHAVIOUR, DISPERSION,
RELATIVE_PROXIMITY, PDU_SESSION_TRAFFIC). "The POI in the NWDAF shall report analytics only when the
target SUPI is the single SUPI for which the analytics is run" (7.16.2.1.1).

What `nfs/nwdaf` does today, against the five records:

| TS 33.128 | Record | `nfs/nwdaf` | Built? |
|---|---|---|---|
| 7.16.2.2 | NWDAFEventsSubscription [152] | Nnwdaf_EventsSubscription POST/PUT/DELETE store real subscriptions (Valkey) | Yes |
| 7.16.2.3 | NWDAFEventsNotification [153] | the notifier thread POSTs a NnwdafEventsSubscriptionNotification per subscription per interval | Yes |
| 7.16.2.4 | NWDAFAnalyticsInfoQuery [154] | Nnwdaf_AnalyticsInfo GET /analytics | Yes |
| 7.16.2.5.2/3 | NWDAFRoamingAnalyticsSubscription / Notification [155]/[156] | Nnwdaf_RoamingAnalytics is not served (Phase D) | No -- nothing to observe |

**Decision.**
1. `li_core::xiri` gains `NwdafEventsSubscription`, `NwdafEventsNotification`,
   `NwdafAnalyticsInfoQuery` and their ENUMERATEDs; response codes map from the HTTP status the NWDAF
   actually returned, and a status with no ENUMERATED value withholds the record (logged) rather than
   carrying another code (the NEF precedent).
2. `nfs/nwdaf/src/li_poi.*` (shared `libs/li-poi` runtime, `config/nwdaf.json` `li_poi`, disabled by
   default, X1 port 7817). Target matching per event: an `eventSubscriptions[]` entry (or the
   query's `tgt-ue`) whose event is one of the seven and whose `tgtUe.supis` is exactly one SUPI, with
   `anyUe` not true -- that SUPI is the one the analytics is run for. A subscription naming several
   single-SUPI entries yields one record per matched SUPI, listing that SUPI's events.
3. **nWDAFConsumerNFType (mandatory) comes from the request's User-Agent** -- TS 29.500 5.2.2.2:
   "This header shall be mainly used to identify the NF type of the HTTP/2 client", its value starting
   with the TS 29.510 NFType. Absent, or an NF type outside the ENUMERATED (pCF, nSSF, aMF, sMF, nEF,
   aF, lMF, nWDAF, dCCF, cEF): the record is withheld and logged, never defaulted (the principle of
   ADR-0488 option B). `oAM(8)` is not an NFType, so it is never produced from a User-Agent.
4. SBIType values: the request body as received for POST/PUT, the stored subscription for DELETE (it
   has no body), the notification as sent, the response body for a 200 query; references exactly as
   the tables give them. nWDAFEventsSubscriptionID = the resource URI (`/nnwdaf-eventssubscription/v1/
   subscriptions/{id}`, the Location this NWDAF returns), present when the operation succeeded.
5. Events notification: reported when the NWDAF sends the Notify (whatever the consumer answers),
   for each matched SUPI whose subscribed events appear in the notification.

**Disclosed.**
- **No NF in this repo sends User-Agent** (TS 29.500: "should be included in every HTTP/2 request" --
  `libs/sbi-core`'s client never sets it). So a subscription from this repo's own DCCF/ADRF yields no
  NWDAFEventsSubscription record; only a consumer that identifies itself does. Adding User-Agent to the
  SBI client is a separate, cross-NF change, not done here.
- SERVICE_EXPERIENCE in this NWDAF is computed per slice and ignores `tgtUe`; matching uses the
  request's `tgtUe` (what the consumer asked for), so a target-SUPI request is reported although the
  answer it gets is not per-UE. UE_MOBILITY / UE_COMMUNICATION / DISPERSION / RELATIVE_PROXIMITY /
  PDU_SESSION_TRAFFIC are not computed (404 to a query, failNotifyCode UNAVAILABLE_DATA in a
  notification) -- both are still real exchanges and are reported with what was actually returned.
- Notifications carry no per-entry UE: an event is attributed to every matched SUPI that subscribed
  to it in that subscription.
- A request whose body or `tgt-ue` does not parse is not reported (no SUPI). 401 responses to a
  parseable request naming a target are reported (the response code has a value for it).
- Table 7.16.2.4-1 types nWDAFAnalyticsData as `NWDAFAnalyticsInfoResponse`; the j70 ASN.1 has
  `SBIType` -- the ASN.1 is what is encoded.

**Rejected alternatives.** Consumer NF type from the access token's `sub` (an NRF NFProfile lookup
per record): needs OAuth on and an NRF round trip inside the POI, and TS 29.500 already names
User-Agent for this. Defaulting an unknown consumer to some value: false content in a mandatory field.
Matching a multi-SUPI or `anyUe` subscription: 7.16.2.1.1 forbids it.

**Verified (2026-10-14).** `Xiri.NwdafRecordsRoundTrip`, `Xiri.NwdafResponseCodesCoverExactlyTheEnumeratedStatuses`;
`LiNwdafEndToEnd` (2, real nwdaf + nrf, real LI_X1, loopback MDF2): POST 201 / PUT 200 / DELETE 204
from a `PCF-...` User-Agent each give one NWDAFEventsSubscription with the target's event only (not
NF_LOAD), the resource URI and the body (DELETE: the stored one); the 2 s notifier gives
NWDAFEventsNotification; an ABNORMAL_BEHAVIOUR query gives NWDAFAnalyticsInfoQuery with the status
returned; no User-Agent, a `UDM-...` User-Agent, a two-SUPI tgtUe and a non-target SUPI give nothing.
The NWDAF needs its Doris feature store to start (CI's service; a temporary local container here).

**Update (ADR-0490, 2026-10-14).** Every NF now sends its NFType as User-Agent (set by
`nf_config::load`), so the "no NF in this repo sends User-Agent" disclosure above no longer holds:
a subscription from this repo's DCCF is recorded with dCCF(10). The ADRF is not in the
NWDAFConsumerNFType ENUMERATED, so its subscriptions are still withheld (logged).
