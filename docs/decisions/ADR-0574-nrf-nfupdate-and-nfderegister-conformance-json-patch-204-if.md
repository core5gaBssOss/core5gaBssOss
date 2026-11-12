## ADR-0574: NRF NFUpdate and NFDeregister conformance: JSON Patch, 204, If-Match, 404 (TS 29.510 5.2.2.3-5.2.2.7)

**Date:** 2026-11-12. **Status:** Proposed. ADR-0572 v0.2.0 item 2, first half (the NRF). The
second half, the ADR-0570 audit, is deferred to its own PR (see Disclosed).

**Context.** ADR-0572 B7 lists "NRF PATCH non-conformance and NRF DELETE 204" as open. Read against
TS 29.510 j80 and `TS29510_Nnrf_NFManagement.yaml` (REL-19), the four NFManagement write routes in
`nfs/nrf/src/main.cpp` departed from the spec as follows:

| Route | Before | TS 29.510 |
|---|---|---|
| `PATCH /nf-instances/{id}` | any content type; patch applied and stored before any check; `remove /nfType` or a new `nfInstanceId` accepted; inapplicable op 400; 200 with the profile; `NF_PROFILE_CHANGED` sent on every heart-beat, even an unchanged one; If-Match ignored | 5.2.2.3.1B, 5.2.2.3.2: `application/json-patch+json` PatchItem[] (YAML); "204 No Content" should be returned; 412 for a different entity tag; 409 for "a value of a non-existing IE"; ops executed atomically |
| `DELETE /nf-instances/{id}` | 204 for an id that is not registered | 5.2.2.4.2 step 2b: 404 |
| `PATCH /subscriptions/{id}` | the raw body replaced the whole subscription; a conformant PatchItem[] body threw inside the handler (500) | 5.2.2.5.6: a JSON Patch that replaces `validityTime` only; 204 when the suggested value is accepted; 400 for errors in the patch |
| `DELETE /subscriptions/{id}` | 204 for an unknown id | 5.2.2.7.2 step 2b: 404 |

GET and PUT of an NF instance sent no `ETag`, although the YAML declares one on GET 200 and PUT
200/201, so a consumer had nothing to put in If-Match.

**Decision:**

- `sbi_core::http2::parse_json_patch_body` is new: the front half of `apply_json_patch_body`
  (415 with `Accept-Patch`, 400 `INVALID_MSG_FORMAT` for malformed JSON, a repeated member, a
  non-array or an empty array). `apply_json_patch_body` now calls it and behaves as before, so the
  NEF, NSACF and PCF callers are unchanged. The NRF needs the split because TS 29.510 gives an
  inapplicable patch 409, not the helper's 400.
- `NfRegistry::update(id, edit)` replaces `apply_patch`. Under the registry lock it looks the
  profile up, runs the edit on a copy and stores the result only if the edit succeeds. If-Match,
  patch, validation and store therefore see no concurrent PUT, and a rejected patch leaves the
  profile as it was.
- `nrf::patch_nf_profile` (`nfs/nrf/src/nf_update.cpp`) decides one PATCH: If-Match not satisfied
  (RFC 9110 13.1.1, strong comparison, `*` and lists) 412; an op RFC 6902 cannot apply (missing
  path, failed `test`) 409; a structurally broken PatchItem 400 `INVALID_MSG_FORMAT`; a result
  without `nfInstanceId`/`nfType`/`nfStatus`, a changed `nfInstanceId` or a non-object result 403
  `MODIFICATION_NOT_ALLOWED` (TS 29.500 table 5.2.7.2-1); a result `validate_nf_profile` rejects
  400 `UNSPECIFIED_MSG_FAILURE`, the same answer NFRegister gives the same profile.
- Success is always 204, for an update and a heart-beat (step 2a "should"). `NF_PROFILE_CHANGED`
  is sent only when the stored profile actually changed. A successful PATCH refreshes the
  heart-beat time.
- `ETag` on GET 200 and PUT 200/201: `nrf::profile_etag`, a quoted hash of the serialised profile.
  nlohmann::json keeps members sorted, so equal profiles give equal tags; an unchanged heart-beat
  therefore generates no new tag (5.2.2.3.2 step 2a).
- NFDeregister of an unknown id: 404 ProblemDetails. TS 29.510 table 6.1.7.3-1 and TS 29.500 table
  5.2.7.2-1 define no cause for it, so none is sent; the same holds for 409 and 412.
- UpdateSubscription: `nrf::subscription_validity_time` accepts only `replace /validityTime` with
  an RFC 3339 date-time (400 `INVALID_MSG_FORMAT` otherwise), `SubscriptionRegistry::
  set_validity_time` stores it, 204; unknown id 404 `SUBSCRIPTION_NOT_FOUND`. NFStatusUnSubscribe
  of an unknown id: 404 `SUBSCRIPTION_NOT_FOUND`.
- `hello-nf` accepts 204 as well as 200 for its heart-beat (`libs/sbi-core/src/nrf_heartbeat.cpp`
  already did).

**Rejected alternatives.**

- **200 with the profile (and an ETag) when the PATCH changed something, 204 only for a no-op
  heart-beat.** The spec allows it, but 5.2.2.3.1B 2a says 204 should be returned and the 204
  header table carries no ETag either way; a consumer that wants the new tag issues a GET. One
  rule is simpler to test and to explain.
- **Changing `apply_json_patch_body` to answer 409 for an inapplicable patch.** NEF, NSACF and PCF
  rely on its 400, and their specs do not name 409 for this case.
- **400 for a patch that removes a mandatory IE or changes `nfInstanceId`.** TS 29.510 names no
  status; TS 29.500 table 5.2.7.2-1 defines `MODIFICATION_NOT_ALLOWED` (403) for exactly "modify IE
  which is not allowed to be modified".
- **Applying the subscription patch with RFC 6902.** The NRF assigns no `validityTime` at
  creation, so a `replace` of an absent member would fail RFC 6902 and every conformant update
  would be rejected. The one permitted operation is checked and applied directly.
- **A SHA-256 entity tag.** `std::hash` is enough for a validator that lives as long as the
  in-memory profile, and it is the pattern the bootstrapping route already uses.

**Disclosed.**

- Deferred for future implementation: the ADR-0570 audit (ADR-0572 item 2, second half). A grep
  on 2026-11-12 found 21 NF sources outside the UDM that both read `path_params` and build a client
  URL with no `encode_path_segment`, among them `nfs/nef`, `nfs/pcf`, `nfs/amf`, `nfs/smf`,
  `nfs/nwdaf`. Tracing each value is a cross-NF change of its own, so it gets its own PR and ADR
  with a test that fails without the fix. It is not part of this ADR.
- The NRF assigns no subscription validity time of its own and expires no subscription (5.2.2.5.2
  step 2a says the 201 contains the validity time "as determined by the NRF"). The suggested
  value is stored as sent. Deferred for future implementation.
- The NRF sends no 3xx redirection (single NRF) and does not implement the Shared-Data
  NFUpdate/NFDeregister variants (5.2.2.3.1C/D, 5.2.2.4.3).
- The entity tag is stable within one NRF process only, which matches the in-memory registry
  (profiles are lost on restart anyway).
- PATCH 204 carries no `Content-Location` header; the 5.2.2.3.2 EXAMPLE shows one, but table
  6.1.3.3.3.3-8 does not list it.

**Verified** (2026-11-12, local build of this branch; targeted runs only, no full `ctest`):
`nrf_discovery_authz_tests` 36/36 passed (includes the new `test_nrf_nf_update.cpp` suites);
`conformance_tests --gtest_filter='*JsonPatch*'` 7/7; `integration_tests` filter
`NrfGapClosureIntegration.*:NrfNfUpdateIntegration.*:HelloNf*` 6/6; `sbi_nrf_heartbeat_tests` 5/5.
Fails without the fix: `NrfNfUpdateIntegration.PatchHeartBeatDeregisterAndSubscriptionUpdate`
against an NRF built from `main` (ea535853) fails at its first new assertion (no `ETag` on PUT 201,
`test_nrf_gap_closure.cpp:292`). That assertion is fatal, so the later 204/404/409/412 checks were
not reached against the old binary; this run does not show each of them failing separately.
