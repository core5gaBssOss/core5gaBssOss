## ADR-0487: LI increment 7c -- the NEF IRI-POI: event mapping, first slice AF session with QoS (TS 33.127 7.9, TS 33.128 7.7)

**Date:** 2026-10-14. **Status:** Accepted -- increment 1 (AF session with QoS provision) in progress; user directive "proceed, don't wait for my commands".

**Context.** The LI plan (ADR-0364) step 7 continues with the NEF, after the UDM (ADR-0475) and the
SMSF (ADR-0486). TS 33.128 clause 7.7 (stage 2: TS 33.127 clause 7.9) defines six NEF LI areas.
Target identifiers: SUPIIMSI, SUPINAI, GPSIMSISDN, GPSINAI (CAALevelUAVID only when the NEF is the
UAS NF); GPSI only for parameter provisioning and 5G LAN (7.7.1.2 NOTE). Each area against what
`nfs/nef` actually does today (read from its handlers, not from its route list):

| TS 33.128 | Records | What `nfs/nef` does | Hookable now? |
|---|---|---|---|
| 7.7.2 NIDD | NEFPDUSessionEstablishment / Modification / Release / Unsuccessful / StartOfInterception, xCC | `Nnef_SMContext` Create/Update/Release/Deliver store a context; no SMF calls it; no link from an SM context to an AF NIDD configuration | No: the trigger is "connection to the AF is established" and `gPSI`, `sMFID`, `aFID` are mandatory -- none exists without the NIDD correlation (TS 23.502 4.25) |
| 7.7.3 Device triggering | DeviceTrigger / Replace / Cancellation / ReportNotify | `3gpp-device-triggering` stores the transaction; no SMS/T4 delivery path | Records could be built, but the "report notification" event never happens and nothing is delivered |
| 7.7.4 MSISDN-less MO SMS | NEFMSISDNLessMOSMS | `nnef-smservice` SendSMS accepts the payload; no SMS-SC calls it | Hook exists, no caller |
| 7.7.5 Parameter provisioning | ExpectedUEBehaviourUpdate | `3gpp-cp-parameter-provisioning` stores | Records could be built (store-only) |
| 7.7.6 AF session with QoS | NEFAFSessionWithQoSProvision / Notification | **Real**: create/update/revoke on `3gpp-as-session-with-qos` drive `Npcf_PolicyAuthorization` app sessions; PCF events come back on `nnef-callback/qos-notify` and are relayed to the AF | Provision: yes. Notification: the relay sends PCF's EventsNotification in a non-standard `_npcfEventsNotification` member, NOT a TS 29.122 UserPlaneNotificationData -- the record's mandatory `userPlaneNotificationData` would be false |
| 7.7.7 5G LAN parameter provisioning | NEF5GVNGroupCreation / Update / Deletion / Query | `3gpp-5glan-pp` stores | Records could be built (store-only) |

**Decision.** Increments, each a real hook on real NEF behaviour, in this order:

1. **AF session with QoS provision (7.7.6.1.2)** -- `NEFAFSessionWithQoSProvision` [135] on POST
   (create, 201), PUT / PATCH (update) and DELETE (revoke), with `aFSessionWithQoSResponseCode` the
   status the NEF actually returns, the AsSessionWithQoSSubscription / ...Patch as `SBIType`
   (reference + JSON value, reference strings exactly as the table gives them), `aFID` = the
   `{scsAsId}` path segment. `li_core::xiri` gains the record; `nfs/nef` gains a POI on the shared
   `libs/li-poi` runtime, disabled unless `config/nef.json` `li_poi.enabled`.
2. **AF session with QoS notification (7.7.6.1.3)** -- first make the AF-bound notification a real
   TS 29.122 `UserPlaneNotificationData` (mapping PCF's AfEvent reports onto UserPlaneEvent per
   TS 29.522 / TS 29.122, or disclose where no mapping is defined), then emit
   `NEFAFSessionWithQoSNotification` [136] with the AF's response code.
3. Later candidates, each needing NEF functionality first: NIDD (the TS 23.502 4.25 correlation),
   device triggering (a delivery path), the store-only provisioning APIs (only once they drive a
   real UDM write -- an xIRI for a provisioning that goes nowhere would report an effect that never
   happens).

**Disclosed for increment 1.** `gPSI` is mandatory in the record: a subscription that identifies the
UE only by IP address (ueIpv4Addr/ueIpv6Addr, no `gpsi`) cannot be matched or reported -- the NEF has
no IP-to-GPSI binding (that is a BSF lookup this NEF does not make). SUPI targets are matched only
when a GPSI-to-SUPI association is known to the NEF, which it is not today, so in practice a SUPI
warrant does not trigger this record (only GPSI warrants do). For PATCH/DELETE the GPSI comes from the
stored subscription. Failed operations (4xx) are reported too when the subscription identifies a
target, since the record carries the response code; a request that does not parse is not (no GPSI).

**Rejected alternatives.** Starting with NIDD (the "headline" NEF LI case, with LI_X3): its trigger
and three mandatory fields depend on an SM-context-to-AF correlation the NEF does not have; building
records anyway would invent sMFID/aFID or report an AF connection that never happened. Building all
four store-only areas' records now: each would report a provisioning effect (a trigger delivered, a
UE behaviour or 5G VN group changed in the UDM) that this NEF does not cause. Reporting today's
`_npcfEventsNotification` relay as `userPlaneNotificationData`: false content in a mandatory field.
