## ADR-0586: TMF640 event notifications: hub listeners and best-effort Service events (N1-N5)

**Date:** 2026-11-18. **Status:** Proposed. The user approved procedure list N1-N5 on 2026-11-18.
This is ADR-0572 v0.2.0 item 3, notifications part. It builds on ADR-0585 (DELETE, #34), which this
branch contains.

**Context.** TMF640-ServiceActivation-v4.0.0.swagger.json defines:
- `POST /hub` (`registerListener`): `EventSubscriptionInput{callback, query}`, answered 201 with
  `EventSubscription{id, callback, query}`;
- `DELETE /hub/{id}` (`unregisterListener`): 204, or 404;
- client-side `POST /listener/serviceCreateEvent`, `.../serviceAttributeValueChangeEvent`,
  `.../serviceStateChangeEvent` and `.../serviceDeleteEvent`.

Each event carries `eventId`, `eventTime`, `eventType` and `event.service`. No BSS service in this
repository had a TMF hub before this one.

**Decision:**
- **N1, N2.** `POST /hub` and `DELETE /hub/{id}` live in `bss/provisioning`.
  - `callback` must be an absolute `https` URL of at most 2048 characters with no whitespace, and
    `query` must be a string if present. Anything else is 400.
  - At most 64 listeners are kept; the 65th is answered 409.
  - Listeners are kept in memory.
- **N3-N5.** After a successful operation the route publishes:
  - `POST /service`: `ServiceCreateEvent`;
  - `PATCH /service/{id}`: `ServiceAttributeValueChangeEvent`, plus `ServiceStateChangeEvent` when
    `state` differs from the state before the change (`patch_service` now reports it);
  - `DELETE /service/{id}`: `ServiceDeleteEvent` (`delete_service` now returns the deleted Service).
- **Delivery.** `POST {callback}/listener/<eventType with a lower-case first letter>`, the swagger's
  `/listener/*` paths. One worker thread drains a queue of at most 1024 deliveries, with a 2 s
  connect timeout and a 5 s total timeout. There is no retry. A failed delivery is logged and
  counted, and a full queue drops the delivery and counts it.
- **No token.** The hub's client has its own token source, which returns nothing, so no NRF access
  token is ever sent to a callback. That token would otherwise come from the process-wide default
  token source (ADR-0499), and the URL is chosen by the API caller.
- Code: `bss/provisioning/src/tmf640_events.{hpp,cpp}` (`EventHub`, `make_event`, `listener_path`,
  `validate_listener`) and `main.cpp`.
- Tests:
  - `Tmf640Events.*` (conformance, pure) cover the paths, the event shape and the callback checks.
  - `Tmf640EventsIntegration.DeliversToTheListenerPathWithoutAToken` uses a real in-process HTTPS
    listener with a default token source installed. It checks that both events arrive at their
    listener paths with no `Authorization` header, and that unregistering works.

**Rejected alternatives.**
- **Persisting listeners in the orchestration database.** Listeners would survive a restart, but
  that adds a table and migration for a lab feature. Deferred for future implementation; a restart
  forgets the listeners, as disclosed.
- **Retry with backoff.** The plan the user approved is best effort with no retry. Retry also needs
  persistence to mean anything.
- **Delivering synchronously inside the request.** A slow listener would hold the TMF640 caller and
  the row lock.
- **Applying `query` as a filter.** The TMF query grammar is not defined in the v4 swagger, so a
  filter would be invented. `query` is stored and echoed only.

**Disclosed.**
- The URL shape `{callback}/listener/<eventName>` is a reading of the TMF convention and the
  swagger's client paths, not swagger text.
- The optional event fields `fieldPath`, `correlationId`, `domain`, `title`, `description`,
  `priority` and `timeOcurred` are not set.
- Callbacks are reached over the lab mTLS client. The client presents the provisioning certificate
  and trusts only the lab CA, so a listener outside the lab PKI cannot be reached. HTTP/2 is
  negotiated by ALPN, with a fallback to HTTP/1.1.
- **SSRF.** Any authenticated caller of the provisioning API can make it POST to an https URL of its
  choice. The body is the Service JSON. No token is sent, and the lab PKI limits who answers. This is
  disclosed, not mitigated.
- The `/hub` routes have no authorization of their own beyond the server's mTLS. This is the same as
  the other TMF640 routes here.
- Verified locally only with `-fsyntax-only` on the five changed translation units. The CI result is
  to be added.
