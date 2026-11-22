## ADR-0606: NEF encodes the traffic-influence id and the UE ids it puts into UDR and UDM URIs (ADR-0570 class, NEF part 2)

**Date:** 2026-11-21. **Status:** Proposed.

**Context.** ADR-0549 made the sbi_core server percent-decode each path parameter, so a value such
as `victim%3F` reaches a handler as `victim?`. ADR-0570 (UDM) and ADR-0575 (NEF, part 1) encode
such values where a peer URI is built. ADR-0603 (PR #54) does the same for the AUSF. This is a second pass
over the NEF only: every outbound `ClientRequest::url` in `nfs/nef/src/` was traced back to its
source on 2026-11-21.

ADR-0575's audit missed one path-derived site, because the concatenation sits in a helper and not
in the route: `push_influence_to_udr` (`main.cpp:430` before this change). The four
`3gpp-traffic-influence` routes (POST, PUT, PATCH, DELETE) pass it `{afId}-{subscriptionId}`, built
from the decoded `afId`, and it was pasted into
`/nudr-dr/v2/application-data/influenceData/{influenceId}` as is. An AF that POSTs to
`/3gpp-traffic-influence/v1/victim%3F/subscriptions` made the NEF PUT
`.../influenceData/victim?-<subId>`, and the UDR stored the record under `victim`, a resource id
another AF's records share a namespace with. That is the ADR-0575 bug on another UDR resource.

ADR-0575 also deferred four body- or record-derived sites in the NEF. They are fixed here:

| Site (before this change) | Value | Source | Goes into |
|---|---|---|---|
| `main.cpp:430` | `{afId}-{subscriptionId}` | request path (`afId`) + NEF-assigned id | UDR `influenceData/{influenceId}` (PUT, DELETE) |
| `main.cpp:3338` | `supi` | AAnF answer (`AkmaAfKeyData.supi`) | UDM `/nudm-sdm/v2/{supi}/id-translation-result` |
| `main.cpp:7494` | GPSI `extid-{externalId}` / `msisdn-{msisdn}` | AF request body | UDM `/nudm-ee/v1/{ueIdentity}/ee-subscriptions` (POST) |
| `main.cpp:7631` | stored GPSI | the record of the POST above | UDM `.../{ueIdentity}/ee-subscriptions/{id}` (DELETE) |
| `main.cpp:8639` | stored GPSI | the record of the POST above (expiry sweep) | the same DELETE |

The `externalId` site is the strongest of the four: the AF chooses it, and an `externalId` such as
`x/../y` or `x?` would make the NEF subscribe at, or delete from, another UE's ee-subscriptions.

**Decision:**

- Each of the five values is encoded as one path segment with
  `sbi_core::uri::encode_path_segment` where the URL is built. The decoded value stays the NEF's
  store key and log field (the ADR-0570 rule). For the influence id the whole
  `{afId}-{subscriptionId}` is one segment, as ADR-0575 did for serviceParamData.
- Test `NefEncodedAfId.TrafficInfluenceAfIdWithQueryCharacterNeverLandsOnAnotherUdrResource`
  (`tests/integration/test_nef_encoded_af_id.cpp`; real NRF, UDR and NEF over TLS 1.3 + mTLS with
  real tokens): an afId of `victim-adr0606%3F` must leave the influence record at
  `victim-adr0606?-<subId>` (UDR DELETE 204) and nothing at `victim-adr0606` (404).

**Deliberately left as they are.**

- `app_session_id` in the PCF PATCH and `/delete` URLs (`main.cpp:520`, `:538`), the TSCTSF
  session id (`:7926`) and the UDM ee-subscription id (`:7631`, `:8639`). Each is the last segment
  of the peer's own `Location` header, which is already in URI form. Encoding it again would turn a
  `%` the peer wrote into `%25`, so it is used as given.
- `nef_instance_id` in NFRegister (`:649`) is the NF's own configured id; the NRF probe URL is a
  constant.
- Consumer-supplied callback URIs (`notificationDestination`) are used as given, as before.
- The NEF's own resource URIs that echo the decoded `afId` / `scsAsId` back to the AF unencoded:
  the `Location` header of many `create` routes and the `transaction` / `subscription` members of
  the QoS and monitoring notifications (`:7263`, `:7360`). They are not requests to a peer, so they
  are outside this class; an AF that chose an afId with `/` or `?` gets back a URI that does not
  name its resource. Deferred for future implementation.

**Rejected alternatives.**

- **Rejecting an afId, externalId or msisdn with reserved characters.** TS 29.122 and TS 29.522
  type them as plain strings, and ADR-0570 and ADR-0575 rejected the same restriction.
- **Encoding where the value is read.** The same value is the NEF's store key and is logged.
- **Encoding the Location-derived ids too.** They are already URI text (above).

**Disclosed.**

- Only the traffic-influence site has a test. The Nudm_SDM and Nudm_EE sites have none: a test
  needs a real UDM and UDR behind the NEF and a monitoring subscription brokered to the UDM, which
  no NEF integration test sets up today. The same helper is applied at each site, but those four
  are checked by compiling only.
- Other NFs are not touched. ADR-0575 lists body-derived sites in the SMF, AMF, PCF and
  bss/provisioning that remain deferred for future implementation.

**Verified (2026-11-21, this machine).**

- `nfs/nef/src/main.cpp` and `tests/integration/test_nef_encoded_af_id.cpp` compile
  (`-fsyntax-only` with the build's flags); both are clang-format-18 clean.
- The new test was not run here: it needs the lab `certs/`, which this worktree may not read. It
  has not yet been shown to fail without the fix.
- Run by the lead in the
  main checkout (2026-11-21), one Debug build of main ea82f83f with the code and tests of PRs #55, #56 and #57 applied together: `NefEncodedAfId.*` passes (2 tests, the ADR-0575 one included). With
  `nfs/nef/src/main.cpp` reverted to ea82f83f and `nef` rebuilt, the new
  `TrafficInfluenceAfIdWithQueryCharacterNeverLandsOnAnotherUdrResource` fails at
  `test_nef_encoded_af_id.cpp:154` (404 for the record's own resource) and `:156` (204 for the
  other resource); the ADR-0575 test still passes. Targeted test only, not a full `ctest`.
