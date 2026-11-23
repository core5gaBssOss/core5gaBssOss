## ADR-0626: PCF ProblemDetails carry invalidParams (SBI-03 step 2, PCF)

**Date:** 2026-11-23. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that takes an
`InvalidParam` list, and builders for the `param` encodings of `TS29571_CommonData.yaml` (a JSON
Pointer for a body attribute, `"query "` plus the name for a query parameter, `"header "` plus the
name for a header, `{name}` for a path variable). TS 29.500 V19.7.0 Table 5.2.7.2-1 NOTE 1:
"\"invalidParams\" attribute shall be included in the \"ProblemDetails\" data structure indicating
unsupported, missing or incorrect IE(s) or query parameter(s) or 3gpp-Sbi-Discovery-* header(s)."
This ADR is step 2 for one NF, the PCF (`nfs/pcf`). UDSF (ADR-0608), UDR (ADR-0609), SEPP
(ADR-0610) did the same before; NEF (ADR-0623), ADRF (ADR-0624) and NWDAF (ADR-0625) are open in
parallel.

**Decision.** Every PCF site that answers a NOTE 1 cause itself now sends invalidParams naming the
member the handler checked. A count over `nfs/pcf/src` finds two such sites, both in `main.cpp`;
`stores.cpp`, `v2x.cpp` and `ue_policy_delivery.cpp` build no SBI error response. No PCF site
answers a query-parameter, header or path-variable NOTE 1 cause, or `MISSING_PARAMETER`. Names were
checked against the REL-19 YAML (`TS29514_Npcf_PolicyAuthorization.yaml`,
`TS29534_Npcf_AMPolicyAuthorization.yaml`).

| Site | Operation | Cause (unchanged) | invalidParams[].param (reason) | Probed |
|---|---|---|---|---|
| `main.cpp` POST `/npcf-policyauthorization/v1/app-sessions` | Npcf_PolicyAuthorization_Create, `AppSessionContext.ascReqData` absent | `MANDATORY_IE_MISSING` | `/ascReqData` ("missing") | yes |
| `main.cpp` PATCH `/npcf-am-policyauthorization/v1/app-am-contexts/{appAmContextId}` | Npcf_AMPolicyAuthorization_Update, the merged `evSubsc` is not an `AmEventsSubscData` | `dto_error_cause(e)` | `/evSubsc/eventNotifUri` ("missing") when the merged object has no `eventNotifUri`; else `/evSubsc` (no reason) | yes (the `eventNotifUri` branch) |

- Site 1 is reachable: the generated `AppSessionContext` reads all three members as optional, so
  `{}` passes `parse_json_body` and reaches the check.
- Site 2 is reachable: `AppAmContextUpdateData.evSubsc` is an `AmEventsSubscDataRm`, all members
  optional, so a body `{"evSubsc":{"events":[{"event":"SAC_CH"}]}}` against a context with no
  events subscription parses, and the merge result lacks `AmEventsSubscData`'s one required member
  `eventNotifUri`. The generated `from_json` reads `eventNotifUri` first, so when it is absent that
  is the failure and the cause is `MANDATORY_IE_MISSING`; the pointer then names it. The `/evSubsc`
  branch (any other failure) is believed unreachable: `events` items are the same `AmEventData`
  in both the Rm and the non-Rm type, so a bad item fails in `parse_json_body` first. It is kept
  as the honest fallback and is not tested.
- No status code, cause, title or detail changed.

Test: `tests/integration/test_pcf_invalid_params.cpp` (2 tests, `PcfInvalidParams.*`, in the
`integration_tests` binary, last in its source list). It spawns a real NRF and a PCF
(`config/pcf.json`, port 7783), waits with `nf_test::wait_until_ready` (ADR-0617) and asserts the
exact `cause` and `invalidParams` array of each probe. The PCF keeps these resources in memory and
reaches no peer on these paths; it needs only the NRF (registration from a background thread, and
the NRF's tokens for the test client) and the lab certificates.

Route shadowing (sbi-core takes the first matching route): POST `/app-sessions` is the first route
registered under `/npcf-policyauthorization/v1`, and PATCH `/app-am-contexts/{appAmContextId}` the
first PATCH under `/npcf-am-policyauthorization/v1`; no earlier route can match either.

**Disclosed.**
- *Out of scope, as in ADR-0607/0609.* A body the generated DTO rejects is answered by sbi-core's
  `parse_json_body` with `MANDATORY_IE_MISSING` / `MANDATORY_IE_INCORRECT` and no invalidParams
  (the nlohmann exception gives no JSON Pointer). That is the answer for most malformed PCF bodies.
- *`/evSubsc` fallback names no member.* If that branch were ever reached, the handler knows only
  that the merged object failed, not which member.
- *Causes that look doubtful, left as they are (a cause change is outside this step).*
  Npcf_PolicyAuthorization_Update answers an `ascReqData` that is not an object with 400
  `UNSPECIFIED_MSG_FAILURE`; `OPTIONAL_IE_INCORRECT` looks the better fit, and it would then need
  invalidParams. The N1 message notification without a `application/vnd.3gpp.5gnas` part answers
  `INVALID_MSG_FORMAT`, which a reader could argue is `MANDATORY_IE_MISSING`. UpdateBDTPolicy with
  `bdtPolData` answers 400 `UNSPECIFIED_MSG_FAILURE` because this build has no transfer policies to
  select; that is a capability gap, not a request error, and the status itself is debatable.
- `ascReqData` is not `required` in the YAML's `AppSessionContext`; the handler requires it on
  create (an application session with no request data has nothing to authorize), so the cause
  `MANDATORY_IE_MISSING` is the handler's, kept unchanged.

**Verified.** `clang-format-18 --dry-run -Werror` on `nfs/pcf/src/main.cpp` and the new test, and
a syntax-only compile (`-fsyntax-only`, flags from the main checkout's `compile_commands.json`) of
both. No full build and no test run in this worktree: the integration tests need the lab
certificates, which this worktree does not read.

**Verified by the lead, 2026-11-23, main checkout (runner 23 idle).** Tree: origin/main 9119d75f plus #77 (AUSF) and #78 (PCF) applied together; the only conflict was both test files appended to the integration_tests list, resolved by keeping both. Full build. `AusfInvalidParams.*` 1/1 and `PcfInvalidParams.*` 2/2 pass. With nfs/ausf/src/main.cpp and nfs/pcf/src/main.cpp restored to main and rebuilt, all 3 fail. Regression: `--gtest_filter='Ausf*:Pcf*'` 25/25 pass. One of them, PcfChfN28Integration.FullLoopSubscribeStatusChangeNotifyUnsubscribe, failed on the first run only because the local Doris had no `chf_cdr` database (the CHF exits at startup). It passed once nfs/chf/schema.doris.sql was applied as CI does. Only these filters were run, not a full ctest.

**Rejected alternatives.**
- *Parse the member name out of `e.what()` at the `evSubsc` site.* nlohmann's message text is not
  an interface; the handler checks the one required member directly instead.
- *Always name `/evSubsc` at that site.* True but less useful: the reachable failure has one
  specific missing member, which the handler can name without guessing.
- *Change the doubtful causes listed above in the same PR.* It changes behaviour consumers see;
  each belongs in its own change with its own test.
