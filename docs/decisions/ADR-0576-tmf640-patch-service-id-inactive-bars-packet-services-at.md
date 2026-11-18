## ADR-0576: TMF640 PATCH /service/{id}: inactive bars packet services at the UDR, active clears it (ADR-0558 P3)

**Date:** 2026-11-15. **Status:** Accepted (user, 2026-11-18). Proposed (procedure list approved by the user, 2026-11-15: PATCH
also updates the stored-only fields; `Service_Update` is applied as merge-patch; the UDR path was
to be checked first and extended only if needed). Implements ADR-0558 increment 1, step P3, and
`docs/PROVISIONING_MAPPING.md` row A4. Item 3 of the v0.2.0 plan (ADR-0572).

**Context.** ADR-0569 shipped TMF640 `POST`/`GET` and deferred PATCH. Row A4 says `inactive` maps
to `AccessAndMobilitySubscriptionData.odbPacketServices = ALL_PACKET_SERVICES`
(TS29503_Nudm_SDM.yaml; type `OdbPacketServices`, TS29571_CommonData.yaml; semantics TS 23.015) and
`active` clears it. The TMF640 swagger (`specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json`)
gives `PATCH /service/{id}` a `Service_Update` body and the responses 200, 400, 401, 403, 404, 405,
409 and 500, and does not say how the body is applied. The UDR OAM `PUT` stores `amData` as the JSON
it is given (`nfs/udr/src/oam_provisioning.cpp` validates `amData` only as an object), so the field
needs no UDR change.

**Decision:**

- **Merge semantics: RFC 7396 merge-patch** (user's answer). `application/merge-patch+json` and
  `application/json` are accepted; any other content type is answered 400 (TMF640 lists no 415).
  `null` removes a member; `serviceCharacteristic` is an array, so a patch replaces it whole.
- **What PATCH changes.** `state` and the stored-only fields (`startDate`, `endDate`, `name`,
  `description`, `note`, ... rule A6). Refused 400: a change of `id`, `href`, `category` (A7),
  `serviceSpecification.id` (A1), `supi` or `msisdn`; a credential-named or duplicate
  characteristic (A3, names never echoed); a state outside `ServiceStateType`; `terminated` (A5); a
  move from `active`/`inactive` back to `feasibilityChecked`/`designed`/`reserved` (the subscriber is
  already at the UDR, so the state would be false).
- **Network action** (`plan_patch`, pure): into `active` from any other state, the network
  documents are written without ODB; `active` to `inactive`, the documents are written with
  `amData.odbPacketServices = "ALL_PACKET_SERVICES"`; every other change (same state, pre-active to
  pre-active, pre-active to `inactive`) writes nothing.
- **Clearing.** The UDR PUT replaces `amData` as a whole, so `active` leaves the member out; the
  mapping row's `null` is not sent. The result is the same stored document.
- **Atomicity.** `ServiceStore::patch` takes the row with `SELECT ... FOR UPDATE`, runs the UDR
  write inside the transaction and updates the row before commit: a UDR 409 (no credentials) is
  answered 409, any other UDR failure 500, and nothing is stored. Two PATCHes of one Service run one
  after the other.
- `UdrAdapter::build_network_documents` and `provision_network` take `odb_barred` (default false).
- Response 200 with the full merged `Service`; errors use the TMF640 `Error` schema.

**Rejected alternatives.**

- **Send `odbPacketServices: null` on `active`.** Equivalent on a whole-document PUT, and it would
  put a `NullValue` in the stored document that every reader must then understand.
- **A UDR PATCH route for the one field.** Would add an OAM route (ours, not 3GPP) to avoid
  rebuilding documents the adapter already builds; the PUT path needs no UDR change.
- **Allow `active`/`inactive` back to a pre-active state.** Would leave a subscriber at the UDR
  while the Service says it was never activated.
- **Allow `supi`/`msisdn` to change.** It moves the subscriber's identity; one Service per SUPI
  (ADR-0569), so a new Service is the honest operation.

**Disclosed.**

- AMF/SMF do not enforce `odbPacketServices` yet (ADR-0558 A4): `inactive` writes the barring data
  and nothing in the core acts on it. Not built, mapping row A4.
- `Service_Update` is not validated through the generated DTO before merging (a `null` would fail
  it); the merged object is validated as a `Service`.
- The orchestration DB transaction stays open during the UDR call, as in ADR-0569.
- DELETE, the `fields` parameter, event notifications (P5) and `/monitor` remain deferred.
- The 400 for an unsupported content type is a choice; the swagger names no 415.

**Verified (2026-11-15, this machine).** `test_tmf640_service` was built and run here: 15 of 15
pass (8 existing, 7 new `Tmf640Patch` tests: both transitions, every no-write move, merge-patch
null, the 400 refusals, no value echoed). The adapter test
`OdbBarringIsOnlyInTheBarredDocuments` and the end-to-end
`Tmf640PatchBarsAndRestoresPacketServices` (UDR, NRF and orchestration DB) compile here but were
**not run** here (no local UDR database); they run in CI.
