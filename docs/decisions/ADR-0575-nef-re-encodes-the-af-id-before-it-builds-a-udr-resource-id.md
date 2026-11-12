## ADR-0575: NEF re-encodes the AF id before it builds a UDR resource id or a callback URI (ADR-0570 class, NEF part)

**Date:** 2026-11-12. **Status:** Proposed. ADR-0572 v0.2.0 item 2, second half (the ADR-0570
audit), NEF part. Follows ADR-0549 (the server percent-decodes path parameters) and ADR-0570 (the
UDM fix for the same class).

**Context.** ADR-0570 fixed the UDM and deferred an audit of every other NF that builds a peer URI
from a decoded request-path value. The audit was done on 2026-11-12 by tracing every outbound
`ClientRequest::url` built by concatenation in `nfs/` and `bss/` back to its source.

*Path-derived values (the ADR-0570 class), all in the NEF* (`nfs/nef/src/main.cpp`, line numbers
before this change):

| Site | Value from the path | Goes into |
|---|---|---|
| 6457, 6516, 6565 | `afId` (+ `configurationId`) | UDR `application-data/iptvConfigData/{afId}-{id}` (PUT, PATCH, DELETE) |
| 6854, 6933, 6975 | `afId` (+ NEF-assigned subscription id) | UDR `application-data/serviceParamData/{afId}-{subId}` (PUT, PATCH, DELETE) |
| 7400 | `scsAsId` | `callbackReference` handed to the UDM (Nudm_EE) |
| 7607, 7701 | `scsAsId` | notification URI handed to the PCF (QoS) |

Example: an AF POSTs to `/3gpp-service-parameter/v1/victim%3F/subscriptions`; the NEF's handler
sees `victim?` and PUT `.../serviceParamData/victim?-<subId>`, so the UDR stored the document under
`victim`, a resource id another AF's `victim-...` records share a namespace with.

Every other concatenated peer URL is either the NF's own instance id (NFRegister) or a value from
a request body, a stored record or a peer response. Those are listed under Disclosed.

**Decision:**

- The six UDR sites encode the whole resource id as one segment,
  `sbi_core::uri::encode_path_segment(af_id + "-" + id)`, at the point the URL is built (the
  ADR-0570 rule: the decoded value stays the local key and log field).
- The three callback URIs encode `af_id` and `sub_id` as separate segments. The NEF's own
  `/nnef-callback/v1/.../{afId}/{subscriptionId}` routes decode them again (ADR-0549), so the
  inbound notification still finds its subscription.
- Test `NefEncodedAfId.ServiceParameterAfIdWithQueryCharacterNeverLandsOnAnotherUdrResource`
  (`tests/integration/test_nef_encoded_af_id.cpp`): real NRF, UDR and NEF; an afId of `victim%3F`
  must leave the document at `victim?-<subId>` (UDR DELETE 204) and nothing at `victim` (404).
  TS29504_Nudr_DR defines no GET for an individual serviceParamData, so DELETE is the probe.

**Rejected alternatives.**

- **Rejecting an afId with reserved characters at the NEF.** TS 29.122 and TS 29.522 type `afId` /
  `scsAsId` as a plain string; ADR-0570 rejected the same restriction for the UDM's ueId.
- **Encoding where the value is read.** The same value is the key of the NEF's own stores and is
  logged; encoding it there would change both (ADR-0570).
- **Fixing the body-derived values in the same change.** They are a different source and need
  their own trace (below); one class per PR keeps the review small.

**Disclosed.**

- Deferred for future implementation: peer URLs built from a request body, a stored record or a
  peer response with no encoding. NEF `:3316` (`supi`, Nudm_SDM id-translation), `:7418`, `:7555`,
  `:8550` (`gpsi` / stored UE id, Nudm_EE); AUSF `:372`, `:771` (`supiOrSuci`, Nudm_UEAU); SMF
  `:2092` (stored `supi`, Namf_Communication); AMF `ngap_task.cpp:1644` (`supi`, Nsmsf_SMService);
  bss/provisioning `udr_adapter.cpp:155`, `:200` (TMF640 `supi`, UDR OAM); PCF `:883` (binding id
  from a BSF `Location`). Each needs its own check of whether the value is validated upstream.
- The NEF's iptvConfigData and the callback paths are not covered by the new test; the same helper
  is applied at each site and the change is mechanical, but only the serviceParamData PUT is
  exercised end to end.

**Verified.** See the PR; results are recorded there once measured.
