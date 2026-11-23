## ADR-0609: UDR ProblemDetails carry invalidParams (SBI-03 step 2, UDR)

**Date:** 2026-11-21. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that takes an
`InvalidParam` list and four builders for the `param` encodings of `TS29571_CommonData.yaml`
(`InvalidParam.param`: a JSON Pointer for a body attribute, `"query "` plus the name for a query
parameter, `"header "` plus the name for a header, `{name}` for a path variable). TS 29.500
V19.7.0 Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the
'ProblemDetails' data structure indicating unsupported, missing or incorrect IE(s) or query
parameter(s) or 3gpp-Sbi-Discovery-* header(s)." This ADR is step 2 for one NF, the UDR
(`nfs/udr`).

**Decision.** Every UDR site that answers a NOTE 1 cause as a literal now sends invalidParams,
naming only what the handler itself knows. A recount on this branch finds 15 such sites, all in
`nfs/udr/src/main.cpp` (none in `stores.cpp`, `oam_provisioning.cpp` or the headers):

| Route (handler) | Cause | invalidParams[].param |
|---|---|---|
| `expiry_problem`, reached from 12 subscribe / modify handlers | `OPTIONAL_IE_INCORRECT` | `/expiry` (reason: the `confirm_expiry_in` message) |
| GroupIDmap `GET /routing-ids` | `MANDATORY_QUERY_PARAM_MISSING` | each absent one of `query nf-type`, `query nf-group-id` |
| GroupIDmap `GET /nf-group-ids` | `MANDATORY_QUERY_PARAM_MISSING` | each absent one of `query nf-type`, `query subscriberId` |
| `GET .../{ueId}/nidd-authorization-data` | `MANDATORY_QUERY_PARAM_MISSING` | each absent one of `query single-nssai`, `query dnn`, `query mtc-provider-information` |
| same, single-nssai not JSON | `MANDATORY_QUERY_PARAM_INCORRECT` | `query single-nssai` (reason "not valid JSON") |
| same, single-nssai without an integer sst | `MANDATORY_IE_MISSING` | `query single-nssai` (reason "sst is missing or not an integer") |
| `GET .../group-data/group-identifiers` | `MANDATORY_IE_MISSING` | `query ext-group-id` and `query int-group-id` (reason: one of them is required) |
| `GET .../context-data` | `MANDATORY_QUERY_PARAM_MISSING` | `query context-dataset-names` |
| `GET .../subscription-data/subs-to-notify` | `MANDATORY_QUERY_PARAM_MISSING` | `query ue-id` |
| `GET .../5g-vn-groups/internal` | `MANDATORY_QUERY_PARAM_MISSING` | `query internal-group-ids` |
| `GET .../mbs-group-membership/internal` | `MANDATORY_QUERY_PARAM_MISSING` | `query internal-group-ids` |
| `PATCH .../aiot-device-profile-data/{id}`, body not an array | `MANDATORY_IE_INCORRECT` | `""` (RFC 6901: the whole body) |
| `GET .../aiot-device-profile-data` | `MANDATORY_QUERY_PARAM_MISSING` | `query requester-aiot-devices-id` |
| `PATCH .../aiot-device-profile-data`, body not an array | `MANDATORY_IE_INCORRECT` | `""` (the whole body) |
| same, no supported-features | `MANDATORY_QUERY_PARAM_MISSING` | `query supported-features` |

- Two helpers in `main.cpp`'s anonymous namespace: `missing_query_params(req, names)` lists only
  the required query parameters the request lacks (the three multi-parameter checks above used to
  say "both" / "all" without saying which), and `expiry_at()`, the one `/expiry` JSON Pointer that
  every handler hands to `confirm_expiry_in` and now also to `expiry_problem`, so the pointer in
  invalidParams is the one that was checked.
- No status code, cause, title or detail changed.

**Second change: the Subs To Notify collection GET was unreachable.** Running
`UdrInvalidParams.MissingUeIdNamesTheQueryParameter` on 2026-11-22 showed that `GET
/nudr-dr/v2/subscription-data/subs-to-notify` answered 200 `{}`. The sbi-core router takes the first
route that matches (`Http2RoutePrecedence.LiteralSegmentIsShadowedWhenRegisteredAfterTheParameterRoute`),
and `/subscription-data/{ueId}` was registered before the literal `subs-to-notify` route, so the
request was answered as the subscribed data of a UE called `subs-to-notify`. This was already the
case on `main`: the collection GET (ADR-0149) could never be reached, with or without `ue-id`. The
collection GET is now registered before `/subscription-data/{ueId}`; the other Subs To Notify routes
stay where they were (none of them collides). A script that compares the `add_route` registrations
of every NF and BSS service on `main` (same method, an earlier parameter segment where a later
route has a literal) found this as the only shadowed route. It sees only `add_route` calls whose
path is a literal or a named `kApiRoot` variable in the same file. The user approved putting this
fix in this PR (2026-11-22).

**Out of scope, unchanged.** The UDR's `parse_json_body` sites (422 `MANDATORY_IE_MISSING` /
`MANDATORY_IE_INCORRECT` from a generated `from_json`) still send no invalidParams, as ADR-0607
disclosed: the nlohmann exception gives no JSON Pointer.

**Disclosed.**
- On a modify (JSON Patch), `/expiry` is the member of the patched resource, not a pointer into
  the patch array. `TS29571_CommonData.yaml`'s `reason` description asks a PATCH failure to name
  the failing operation's index; the UDR applies the whole patch before it confirms the expiry,
  so it does not know the index and the reason does not name one.
- The `group-identifiers` site answers `MANDATORY_IE_MISSING` for a missing query parameter; a
  query parameter cause (`MANDATORY_QUERY_PARAM_MISSING`) looks the better fit, but the task was
  not to change causes. Left as is, for a decision of its own.
- The non-array JSON Patch sites name the whole body with the empty JSON Pointer `""`. That is
  RFC 6901's pointer to the whole document and is what the handler knows; a consumer that
  expects a member name gets none.
- 12 `expiry_problem` call sites were changed mechanically (`json::json_pointer("/expiry")` to
  `expiry_at()`); clang-format-18 re-indented the lambdas around some of them, which is the bulk
  of the diff's line count. No logic in those lambdas changed.

**Verified.** See the PR for what was built and run on this machine; the integration tests need
the lab certificates, which this worktree does not read.

**Verified by the lead (2026-11-22, main checkout, master machine with its CI runner idle).** One Debug build of ADR-0607's commit 55ba4d06 with the code and tests of PRs #60 (including the subs-to-notify route fix), #62 and #63 applied together. `integration_tests --gtest_filter='UdrInvalidParams.*'` passes (5 of 5), and `UdrIntegration.OnDataChangeWebhookDeliveredOnPutPatchDelete` (which uses the subs-to-notify collection) still passes. With `nfs/udr/src/main.cpp` restored to 55ba4d06 and `udr` rebuilt, all 5 `UdrInvalidParams.*` tests fail. Before the route fix, `MissingUeIdNamesTheQueryParameter` failed even with the invalidParams change (200 `{}`), which is how the shadowed route was found. Targeted tests only, not a full `ctest`.

**Rejected alternatives.**
- *Name every required query parameter at the multi-parameter sites, present or not.* The NOTE
  asks for the missing ones; listing a present one would be wrong.
- *Hard-code `/expiry` inside `expiry_problem`.* It would silently be wrong for a future caller
  that confirms an expiry at another pointer; passing the pointer keeps the one checked and the
  one reported the same.
- *Turn the `group-identifiers` cause into `MANDATORY_QUERY_PARAM_MISSING` here.* A cause change
  is a behaviour change outside this step.
