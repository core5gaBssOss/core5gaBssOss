## ADR-0576: TMF640 PATCH and DELETE /service/{id}: barring through a UDR OAM am-data write, deprovisioning through a UDR OAM delete

**Date:** 2026-11-12. **Status:** Proposed. ADR-0572 v0.2.0 item 3, first part. Follows ADR-0558
(TMF640 mapping, rows approved 2026-11-07), ADR-0569 (increment 1: POST/GET, network data and
credentials written separately) and ADR-0382 (the UDR's OAM provisioning API).

**Context.** ADR-0569 deferred PATCH (P3: `inactive`/`active`), DELETE and notifications. Approved
mapping row A4 (`docs/PROVISIONING_MAPPING.md`) says `inactive` sets
`AccessAndMobilitySubscriptionData.odbPacketServices` to `ALL_PACKET_SERVICES` and `active` clears
it (`null`). Nudr_DR (TS29505_Subscription_Data.yaml) has only GET on
`provisioned-data/am-data`, and the UDR's OAM API had a full-subscriber PUT and a keys-only PUT,
no DELETE. Row A5 refused `terminated` because "the UDR OAM API cannot delete".

The user chose on 2026-11-12: a new, narrow OAM am-data PUT (not a re-PUT of every document); a
new OAM DELETE so TMF640 DELETE deprovisions; one PR for PATCH, DELETE and the hub notifications.
The hub waits on TMF630 (the delivery URL of an event is not defined in the TMF640 swagger; the
user is fetching TMF630), so it is not part of this ADR.

**Decision:**

*UDR, OAM API (not 3GPP; ADR-0382 envelope rules):*

- `PUT /oam-provisioning/v1/subscribers/{ueId}/am-data`, body `{"servingPlmnId", "amData"}`
  (`amData` an AccessAndMobilitySubscriptionData, TS29503_Nudm_SDM.yaml; `odbPacketServices`, if
  present, a string or null per TS29571_CommonData.yaml `OdbPacketServices`). Replaces that serving
  PLMN's `am_data` column only; 204; 404 when the subscriber has no provisioned data for that PLMN
  (never creates). Same mTLS allow-list as the other OAM routes.
- `DELETE /oam-provisioning/v1/subscribers/{ueId}`: deletes the UE's rows in
  `udr_provisioned_data`, `udr_authentication_subscription`, `udr_am_policy_data` and
  `udr_sm_policy_data` in one transaction; 204, or 404 when none existed.

*BSS, TMF640:*

- Invariant: the UDR holds the subscriber's network data if and only if the Service state is
  `active` or `inactive`. POST therefore now writes barred network data for `inactive` (A4 binds
  every inactive Service, not only a PATCHed one), and a keyless POST `inactive` is 409 like a
  keyless POST `active`.
- PATCH `/service/{id}`: body a JSON merge patch (RFC 7386) of `Service_Update` members only;
  content type `application/json` (the swagger's PATCH media type; parameters ignored). Under the
  row lock, the stored Service is merged, normalised through the generated `Service` DTO, and the
  A-row checks of POST are re-run on the result (`check_service`, shared with POST). 200 with the
  Service.

  | from | to | UDR action |
  |---|---|---|
  | pre-active | pre-active | none (stored only) |
  | pre-active | `active` | full network write, unbarred |
  | pre-active | `inactive` | full network write, barred (409 if no credentials) |
  | `active` | `inactive` | am-data PUT, `odbPacketServices: ALL_PACKET_SERVICES` |
  | `inactive` | `active` | am-data PUT, `odbPacketServices: null` |
  | `active`/`inactive` | pre-active | refused 400 |
  | any | `terminated` | refused 400 (A5) |
  | msisdn change, `active`/`inactive` | same | full network write, barring kept |
  | msisdn change, pre-active | same | none (stored only) |

  Pre-active = `feasibilityChecked`, `designed`, `reserved`. A changed `supi` or
  `serviceSpecification.id` is 400 (a merge patch replaces the whole `serviceCharacteristic`
  array, so a PATCH must carry `supi` unchanged). The row update and the UDR write happen in one
  orchestration-DB transaction, committed only after the UDR write succeeded.
- DELETE `/service/{id}`: the row is deleted inside a transaction; for an `active` or `inactive`
  Service the UDR OAM DELETE runs before commit (a UDR 404 counts as success, so a retry
  succeeds); 204. A pre-active Service wrote nothing at the UDR, so nothing is deleted there.
- Errors keep the TMF640 `Error` body. Status codes are only those the swagger lists: PATCH
  200/400/404/409/500, DELETE 204/404/500. A wrong media type is therefore 400, not 415.
- `terminated` stays refused (A5). The approved A5 row is left as approved; a note under
  "Implementation status" in `docs/PROVISIONING_MAPPING.md` records that its reason ("the UDR OAM
  API cannot delete") no longer holds, because DELETE is now the deprovisioning path.

**Rejected alternatives.**

- **Re-PUT every subscriber document to change the barring.** Rejected by the user; it rewrites
  SM data and policy data to flip one field.
- **Barring only on PATCH, leaving POST `inactive` unwritten.** An inactive Service then has no
  network data, and a later PATCH to `active` has nothing to clear; A4 would hold for some inactive
  Services and not others.
- **`application/merge-patch+json` as the PATCH media type.** The TMF640 v4.0.0 swagger declares
  `application/json;charset=utf-8`; TMF630 is not in `specs/tmforum`, so nothing in hand names
  another type.
- **Deleting at the UDR for a pre-active Service.** The order flow may already have written the
  subscriber's credentials; a Service that never provisioned network data should not remove them.
- **Allowing `active` → `reserved` etc.** It would need the network data removed while the
  subscriber's keys stay; there is no use for it in the approved rows.

**Disclosed.**

- No NF reads `odbPacketServices` yet: the barring is written, not enforced (AMF/SMF enforcement
  deferred for future implementation, as row A4 already says).
- The UDR DELETE removes only what the OAM API writes. Other state keyed by the UE stays: AMF
  registration context, SDM/EE subscriptions, the saga's completed order record in the
  orchestration DB. Deleting the `udr_provisioned_data` row also removes its lcs_bca, sms_mng, sms
  and trace columns, which the OAM PUT does not own.
- A Service stored `inactive` before this change has no network data at the UDR; a PATCH to
  `active` then fails with 500 (the am-data PUT gets 404). Lab data only; recreate the Service.
- If the profile named by a stored Service is removed from config, every PATCH of that Service is
  409 until the profile is restored.
- Event notifications (hub, listeners) are deferred until TMF630 is in `specs/tmforum`.
- `fields`, `/monitor`, re-provisioning to another `serviceSpecification` are deferred for future
  implementation.

**Verified.** See the PR; results are recorded there once measured.
