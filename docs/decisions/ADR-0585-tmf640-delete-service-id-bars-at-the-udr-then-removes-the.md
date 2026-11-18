## ADR-0585: TMF640 DELETE /service/{id} bars at the UDR, then removes the Service

**Date:** 2026-11-18. **Status:** Proposed. On 2026-11-18 the user approved the TMF640 procedure list
for ADR-0572 v0.2.0 item 3 and chose "Bar, then delete" for DELETE. Follows ADR-0558, ADR-0569 and
ADR-0576. Numbered 0585 because ADR-0583 (#32) and ADR-0584 (#33) are open.

**Context.** TMF640-ServiceActivation-v4.0.0.swagger.json defines `DELETE /service/{id}`
(`deleteService`) with responses 204, 400, 401, 403, 404, 405, 409 and 500. Mapping row A5
(`docs/PROVISIONING_MAPPING.md`, approved 2026-11-07) still holds: the UDR OAM API cannot delete a
subscriber. Deleting only the Service row would leave an active subscriber in the network with no
Service, which is the false state that row A5 warns about.

**Decision:**
- `DELETE /service/{id}` loads the Service under a row lock and answers 404 if it is absent.
- If the stored `state` is `active` or `inactive`, the Service's network documents exist at the
  UDR. They are rewritten with `amData.odbPacketServices = ALL_PACKET_SERVICES`, the same write as
  PATCH `inactive` (ADR-0576, row A4).
- The row is then deleted and the transaction commits, and the answer is 204.
- A pre-active Service (`feasibilityChecked`, `designed`, `reserved`) wrote nothing to the network
  (ADR-0569), so nothing is written for it.
- A UDR 409 (the subscriber has no credentials) is answered 409, and any other UDR failure 500. In
  both cases the row is kept, because the UDR write runs before the commit. This is the same shape as
  PATCH.
- A new `POST /service` for the same SUPI is accepted afterwards, and `active` rewrites the documents
  without the bar.
- Code:
  - `plan_delete` is pure.
  - `ServiceStore::remove` and `delete_service` are in `bss/provisioning/src/tmf640_service.*`.
  - The route is in `bss/provisioning/src/main.cpp`.
  - The counter is `provisioning_tmf640_service_delete_total`.
- Tests:
  - `Tmf640Delete.ActiveAndInactiveServicesAreBarredFirst` and
    `Tmf640Delete.PreActiveServicesWriteNothing` are conformance tests and need no services.
  - `UdrOamProvisioningIntegration.Tmf640DeleteBarsThenRemovesTheService` runs against a real NRF,
    UDR and orchestration PostgreSQL. It checks that the Service is gone, the UDR reads
    `ALL_PACKET_SERVICES`, a second DELETE is 404, and a re-create clears the bar.

**Rejected alternatives.**
- **Full deprovisioning: a UDR OAM DELETE that removes the subscriber's provisioned data.** It
  touches a second subsystem, the UDR, so it needs its own PR first. It also needs a decision about
  the subscriber's credentials and context data. Deferred for future implementation; row A5 is
  unchanged.
- **Removing the record only.** The subscriber would stay active in the network with no Service
  (row A5).
- **Answering 409 for an `active` Service, so the client must PATCH it `inactive` first.** The
  swagger does not require that, and it would make two calls of one.

**Disclosed.**
- After DELETE the subscriber is still provisioned at the UDR, barred from packet services. AMF/SMF
  enforcement of `odbPacketServices` is still not built (ADR-0576), so in this lab the bar is
  recorded but not enforced.
- The ServiceDeleteEvent notification (procedure N5) comes with the notifications increment (N1-N5).
  This PR sends no events.
- Verified locally only with `-fsyntax-only` on the four changed translation units, because the
  runners were busy and this host runs CI. The integration test was not run, and the CI result is to
  be added.
