## ADR-0569: TMF640 activates subscriber network data; SIM keys reach the UDR on their own route

**Date:** 2026-11-08. **Status:** Closed (work pushed; last citing commit 1e11ba6d on origin/main, 2026-11-10). Accepted (user chose "Split the UDR write", 2026-11-08).
Implements ADR-0558 increment 1, steps P1 and P2. Amends ADR-0382 (the UDR OAM provisioning API).

**Context.** ADR-0558's increment 1 adds TMF640 `POST /service`, `GET /service` and
`GET /service/{id}` to `bss/provisioning`. The approved mapping (`docs/PROVISIONING_MAPPING.md`,
rows A1-A7, 2026-11-07) says a Service in state `active` means the subscriber's data is written to
the UDR, and row A3 says SIM keys (K, OPc, SQN) never enter a TMF640 Service, because
`GET /service/{id}` returns every `serviceCharacteristic`. The UDR's only write,
`PUT /oam-provisioning/v1/subscribers/{ueId}` (ADR-0382), required `authenticationSubscription`
(the keys) on every call. So a TMF640 activation could not write to the UDR without breaking A3.

**Decision:**

- **UDR OAM API (ours, not 3GPP).** `authenticationSubscription` is optional on
  `PUT /oam-provisioning/v1/subscribers/{ueId}`. Without it the subscriber's credentials row is
  kept, and must already exist: otherwise 409 and nothing is written. A new
  `PUT /oam-provisioning/v1/subscribers/{ueId}/authentication-subscription` writes one
  `AuthenticationSubscription` (TS29505_Subscription_Data.yaml) and nothing else, with the same
  validation, the same mTLS allow-list and the same never-echo rule. 201/204 on the main PUT now
  mean "the subscriber had / had no provisioned data before", not "had no credentials".
- **`UdrAdapter`** gains `provision_credentials` (the keys-only PUT, for the order flow) and
  `provision_network` (the network documents without credentials). For TMF640 the network
  profile is keyed exactly by `serviceSpecification.id` (A1), with no `default` fallback.
  `config/provisioning.json` (and the Helm and lab copies) gains the profile `rfs-5g-data`.
- **TMF640** (`bss/provisioning/src/tmf640_service.{hpp,cpp}`, base path
  `/tmf-api/ServiceActivationAndConfiguration/v4`, bodies are the generated `tmf_gen` DTOs,
  ADR-0568):
  - `POST /service` accepts a `Service_Create` with `category` `RFS` (A7), a `state` from the
    closed `ServiceStateType` set other than `terminated` (A5), a `serviceSpecification.id` that
    names a network profile (A1), a `supi` characteristic in IMSI form and an optional digit
    `msisdn` (A2). Credential-named characteristics (`k`, `opc`, `sqn`, `encPermanentKey`, ...)
    are refused (A3). `startDate`/`endDate` are stored only (A6).
  - `active` writes the network documents to the UDR inside the same database transaction that
    inserts the Service. A UDR 409 (no credentials yet) is answered 409 and any other UDR failure
    500; either way nothing is stored. The other accepted states write nothing to the network.
  - One Service per SUPI (unique `ue_id`); a second is answered 409 before any network write.
  - `GET /service` pages with `offset`/`limit` (limit 1-1000, default 100) and sets
    `X-Total-Count`/`X-Result-Count` (the swagger's GET 200 headers). `GET /service/{id}` answers
    200 or 404.
  - Errors use the TMF640 `Error` schema (`code`, `reason`, `message`, `status`).
  - The Services live in `orchestration.tmf640_service` (`deploy/db/orchestration/schema.sql`).

**Rejected alternatives.**

- **TMF640 only changes the state of a subscriber the order flow already wrote in full.** No
  standalone activation through TMF640, so ADR-0558's P4 (the saga activates through TMF640)
  would not be possible.
- **Keys in a non-TMF part of the POST body, used once and not stored.** TMF640 could activate on
  its own, but its request would no longer be the TM Forum shape, and a client could still send
  the keys to an endpoint that is meant never to see them.

**Disclosed.**

- The order flow (`POST /provisioning/v1/customerOrder`) is unchanged: it still writes keys and
  network data in one PUT. Moving it onto `provision_credentials` + TMF640 is P4.
- PATCH (P3: `inactive` → `odbPacketServices`), DELETE, the `fields` parameter (refused 400),
  event notifications (P5) and `/monitor` are deferred for future implementation.
- The credential-name check is a deny-list, not a proof that no key can be sent under another
  name.
- The orchestration DB transaction stays open during the UDR call; a slow UDR holds one pool
  connection for as long as the client's request timeout.
- `href` and `Location` are paths (`/tmf-api/...`), not absolute URLs, as in the repo's other TMF
  APIs.
- An existing lab database gets `orchestration.tmf640_service` only when `schema.sql` is applied
  again (`CREATE TABLE IF NOT EXISTS`); a fresh lab volume and CI create it.
- The routes keep the provisioning service's default route authentication, as its other routes do.

**Verified (2026-11-08, this machine).**

- All changed sources and both test files compile (`-fsyntax-only` with the build's flags).
- `test_tmf640_service` was built and run here: 8 of 8 tests pass (the A1, A2, A3, A5 and A7
  refusals, the closed state set, no key echoed in any refusal, every pre-active state accepted).
- CI run 42498085937 (PR #14, 2026-11-08), build job: `NetworkDocumentsCarryNoCredentials`,
  `NetworkDataAndCredentialsTravelSeparately`, the existing UDR OAM tests and all 8
  `Tmf640Service` tests passed. Two tests failed, both fixed in the next commit:
  `Tmf640ActivationWritesNetworkDataOnlyAndStoresTheService` could not reach the orchestration DB
  (CI publishes it on port 15434; `ci.yml` now passes `PROVISIONING_ORCHESTRATION_DATABASE_URL`),
  and `gui_nf_config_schemas_up_to_date` (the GUI schema for `provisioning.json` regenerated for
  the new profile).
- CI run 42547589719 on main after the merge (2026-11-09): every job succeeded (build, ASan/UBSan,
  TSan); 1245 of 1245 tests passed, including both tests above and the three new UDR OAM tests.
