## ADR-0625: NWDAF ProblemDetails carry invalidParams (SBI-03 step 2, NWDAF)

**Date:** 2026-11-23. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload and a
`make_problem_details` overload that take an `InvalidParam` list, and builders for the `param`
encodings of `TS29571_CommonData.yaml` (a JSON Pointer for a body attribute, `"query "` plus the
name for a query parameter, `"header "` plus the name for a header, `{name}` for a path
variable). TS 29.500 V19.7.0 Table 5.2.7.2-1 NOTE 1: "\"invalidParams\" attribute shall be
included in the \"ProblemDetails\" data structure indicating unsupported, missing or incorrect
IE(s) or query parameter(s) or 3gpp-Sbi-Discovery-* header(s)." This ADR is step 2 for one NF,
the NWDAF (`nfs/nwdaf`, both roles: AnLF in `main.cpp`, MTLF in `mtlf.cpp`). UDSF (ADR-0608),
UDR (ADR-0609) and SEPP (ADR-0610) did the same before.

**Decision.** Every NWDAF site that answers a NOTE 1 cause now sends invalidParams naming what
the handler itself checked. A recount on this branch finds 11 such sites (4 in `main.cpp`, 7 in `mtlf.cpp`) (all
`MANDATORY_IE_MISSING` or `MANDATORY_QUERY_PARAM_MISSING`; no NWDAF site answers any other NOTE 1
cause, and none names a header or path variable). No NOTE 1 site is in `analytics.cpp`,
`ml_consumer.cpp` or the other NWDAF sources. Every name was checked against the REL-19 YAML
(`TS29520_Nnwdaf_AnalyticsInfo.yaml`, `_MLModelMonitor.yaml`, `_MLModelProvision.yaml`,
`_MLModelTraining.yaml`, `_DataManagement.yaml`).

| Site | Operation | invalidParams[].param (reason) | Probed |
|---|---|---|---|
| `main.cpp` GET `/nnwdaf-analyticsinfo/v1/analytics` | GetNWDAFAnalytics, no `event-id` (the YAML's only required query parameter) | `query event-id` ("missing") | yes |
| `main.cpp` `monitor_validate` (POST/PUT `/nnwdaf-mlmodelmonitor/v1/subscriptions`) | `MLModelMonitorSub.modelIds` empty (minItems 1) | `/modelIds` ("empty; minItems 1") | yes (POST) |
| `main.cpp` POST `/nwdaf-inbound/v1/fetch` | the DataManagement Fetch callback; its body is the array itself (minItems 1) | `""` (RFC 6901: the whole body; "empty; minItems 1") | yes |
| `main.cpp` `serving_source` (POST `/nnwdaf-datamanagement/v1/subscriptions`) | `NnwdafDataManagementSubsc` oneOf, neither present | `/anaSub`, `/dataSub` ("missing; one of anaSub, dataSub is required") | yes |
| `mtlf.cpp` provision `validate` (POST/PUT `/nnwdaf-mlmodelprovision/v1/subscriptions`) | an `MLEventSubscription` without `mLEvent` and without `posModelReqInd` | `/mLEventSubscs/<i>/mLEvent` ("missing") | yes (POST, index 1) |
| `mtlf.cpp` POST `/nnwdaf-mlmodelmonitor/v1/registrations` | `MLModelMonitorReg` oneOf, neither `consumerId` nor `consumerSetId` | `/consumerId`, `/consumerSetId` ("missing; one of consumerId, consumerSetId is required") | yes |
| same | `!dto->modelId` | `/modelId` (no reason) | no, see Disclosed |
| `mtlf.cpp` POST `/nwdaf-inbound/v1/ml-monitor` | `MLModelMonitorNotify` anyOf, neither present | `/modelAccuInfos`, `/anaFeedbacks` ("missing; at least one of modelAccuInfos, anaFeedbacks is required") | yes |
| `mtlf.cpp` training `validate` | `mLEventSubscs` / `notifUri` / `notifCorreId` absent | `/<member>` ("missing") | no, see Disclosed |
| same | an `MLEventSubscription` without `mLEvent` | `/mLEventSubscs/<i>/mLEvent` ("missing") | yes (POST, index 0) |
| `mtlf.cpp` POST `.../subscriptions/{subscriptionId}/unsubscribe-info` | `TrainingUnsubscribeInfo.termCause` absent | `/termCause` ("missing") | no, see Disclosed |

- The two local `problem` helpers (the anonymous-namespace function in `mtlf.cpp`, the lambda in
  `main.cpp`) take an optional `std::vector<sbi_core::InvalidParam>` (default empty) and pass it
  to ADR-0607's `make_problem_details` overload; an empty list leaves the attribute out, so every
  other caller writes the same body as before.
- The two `mLEventSubscs` loops now iterate by index so the JSON Pointer names the item that
  failed, built with `json_pointer / i` (the library escapes, not the caller).
- At the two oneOf sites the condition covers both "neither" (`MANDATORY_IE_MISSING`) and "both"
  (`INVALID_MSG_FORMAT`, not a NOTE 1 cause); invalidParams is attached in the "neither" case only,
  which is the case that has the NOTE 1 cause.
- No status code, cause, title or detail changed.

Test: `tests/integration/test_nwdaf_invalid_params.cpp` (8 tests, `NwdafInvalidParams.*`, in the
`integration_tests` binary, last in its source list). It spawns a real NRF, an NWDAF in role
`anlf` (`config/nwdaf.json`, port 7798) and one in role `mtlf` (port 7797, metrics 9486, as
`test_nwdaf_ml_training_sub.cpp` does), waits with `nf_test::wait_until_ready` (ADR-0617), and
asserts the exact `cause` and `invalidParams` array of each probe. Both NWDAFs keep their state in
Valkey; without Valkey at `NWDAF_REDIS_URL` (default `tcp://127.0.0.1:6379`) the test is skipped,
not passed. No Doris feature store, DCCF, ADRF or Kafka is needed for these error paths.

Route shadowing (sbi-core takes the first matching route): none of the probed routes has an
earlier-registered route with a parameter segment at the same depth (`/nwdaf-inbound/v1/
notifications/{source}` is one segment longer than `/fetch`; every `{subscriptionId}` route is one
segment longer than its collection).

**Disclosed.**
- *Unreachable sites, changed anyway, not tested.* The `NwdafMLModelTrainSubsc` required-member
  check (`mLEventSubscs`, `notifUri`, `notifCorreId`) and the `TrainingUnsubscribeInfo.termCause`
  check run after `parse_json_body`, whose generated `from_json` already throws for an absent
  required member; sbi-core answers 400 `MANDATORY_IE_MISSING` there first, without invalidParams
  (ADR-0607: the nlohmann exception gives no JSON Pointer). Those sbi-core answers are the ones a
  consumer actually gets; they remain out of scope as in ADR-0607/0609.
- *`modelId` site.* The generated `MLModelMonitorReg.modelId` is a non-optional `Uinteger`
  (`std::int64_t`), so `!dto->modelId` is true only for `modelId: 0`, which the handler then
  answers as "modelId is required" / `MANDATORY_IE_MISSING`. A present value 0 is a valid
  `Uinteger`; this looks like a pre-existing bug. invalidParams names `/modelId` without a reason;
  the check itself is unchanged (not a cause change for this step) and is not tested, so as not
  to pin the bug in a test.
- *Causes that look wrong, left as they are.* `modelIds: []` and a Fetch body `[]` are present but
  empty, which reads as `MANDATORY_IE_INCORRECT` rather than `MANDATORY_IE_MISSING`. The malformed
  JSON query parameters of GetNWDAFAnalytics (`event-filter`, `tgt-ue`, `ana-req`) and an empty
  `eventSubscriptions` in Nnwdaf_EventsSubscription answer `UNSPECIFIED_MSG_FAILURE`, not a
  NOTE 1 cause (`OPTIONAL_QUERY_PARAM_INCORRECT` / `MANDATORY_IE_INCORRECT` look the better fit),
  so they carry no invalidParams. Changing causes is a behaviour change outside this step.
- The provision site names only `mLEvent`, the member the handler checks. The YAML's oneOf is
  `[mLEvent, mLEventFilter]` or `[posModelReqInd]`; a subscription with `mLEvent` but no
  `mLEventFilter` is not rejected by this handler, before or after this change.
- The Fetch site's empty JSON Pointer `""` names the whole body (ADR-0609 did the same for the
  UDR's non-array JSON Patch bodies); a consumer that expects a member name gets none, because
  the body has no members.

**Verified.** `clang-format-18 --dry-run -Werror` on the three changed C++ files, and a
syntax-only compile (`-fsyntax-only`, flags from the main checkout's `compile_commands.json`) of
`nfs/nwdaf/src/main.cpp`, `nfs/nwdaf/src/mtlf.cpp` and the new test. No full build and no test run
in this worktree: the integration tests need the lab certificates, which this worktree does not
read.

**Rejected alternatives.**
- *Name only one member at the oneOf / anyOf sites.* Neither is "the" missing one; the handler
  knows only that none of the alternatives is present, so both are named with a reason that says
  one is required (the NEF did the same for `dnns`/`snssais`, ADR-0623).
- *Attach invalidParams to the "both present" oneOf answers too.* Their cause is
  `INVALID_MSG_FORMAT`, which NOTE 1 does not cover; listing members there would be a change of
  its own.
- *Fix the `modelId == 0` check here.* It changes which requests are accepted; it belongs in its
  own change with its own test.
- *A shared NWDAF `problem` helper for both files.* `main.cpp` and `mtlf.cpp` each already had
  their own; merging them is a refactor unrelated to NOTE 1.
