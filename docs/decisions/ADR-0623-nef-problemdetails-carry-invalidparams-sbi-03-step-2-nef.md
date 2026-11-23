## ADR-0623: NEF ProblemDetails carry invalidParams (SBI-03 step 2, NEF)

**Date:** 2026-11-23. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that takes an
`InvalidParam` list and four builders for the `param` encodings of `TS29571_CommonData.yaml`
(`InvalidParam.param`: a JSON Pointer for a body attribute, `"query "` plus the name for a query
parameter, `"header "` plus the name for a header, `{name}` for a path variable). TS 29.500
V19.7.0, Table 5.2.7.2-1, NOTE 1: "\"invalidParams\" attribute shall be included in the
\"ProblemDetails\" data structure indicating unsupported, missing or incorrect IE(s) or query
parameter(s) or 3gpp-Sbi-Discovery-* header(s)." The NOTE 1 causes are `INVALID_QUERY_PARAM`,
`MANDATORY_QUERY_PARAM_INCORRECT`, `OPTIONAL_QUERY_PARAM_INCORRECT`,
`MANDATORY_QUERY_PARAM_MISSING`, `MANDATORY_IE_INCORRECT`, `OPTIONAL_IE_INCORRECT`,
`MANDATORY_IE_MISSING` (400) and `MISSING_PARAMETER` (403). For the NEF's northbound (TS 29.122 /
TS 29.522) APIs, TS 29.122 Table 5.2.6-1 makes the TS 29.500 causes apply (as recorded in
ADR-0536). The UDR (ADR-0609) and SEPP (ADR-0610) are done; this ADR is step 2 for the NEF
(`nfs/nef`). Earlier NEF SBI-03 cause work (ADR-0578, ADR-0601) and the URL-encoding fixes
(ADR-0575, ADR-0606) touch none of these sites and are left as they are.

**Decision.** Every NEF site that answers a NOTE 1 cause as a literal and can name the parameter
now sends invalidParams. A recount on this branch finds 11 literal NOTE 1 sites, all in
`nfs/nef/src/main.cpp` (none in `stores.cpp`, `expiry.cpp`, `li_poi.cpp`, `monitoring_mapping.cpp`,
`qos_mapping.cpp` or the headers); 9 now carry invalidParams:

| Route (API, YAML) | Cause | invalidParams[].param (reason) |
|---|---|---|
| `GET /nnef-pfdmanagement/v1/applications` (Nnef_PFDmanagement AllFetch, `TS29551_Nnef_PFDmanagement.yaml`) without `application-ids` | `MANDATORY_QUERY_PARAM_MISSING` | `query application-ids` ("missing") |
| `POST /nnef-smservice/v1/sm-contexts/{supi}/sendsms` (`TS29541_Nnef_SMService.yaml`) without a Content-Type | `MANDATORY_IE_MISSING` | `header Content-Type` ("missing") |
| `POST /nnef-smcontext/v1/sm-contexts/{smContextId}/deliver` (`TS29541_Nnef_SMContext.yaml`) without a Content-Type | `MANDATORY_IE_MISSING` | `header Content-Type` ("missing") |
| `PATCH /3gpp-mbs-session/v1/mbs-sessions/{mbsSessionRef}` (`TS29522_MBSSession.yaml`), body not a non-empty array | `MANDATORY_IE_INCORRECT` | `""` ("not a non-empty array") |
| `PATCH /3gpp-ims-pp/v1/pp/{ppId}` (`TS29522_ImsParamProvision.yaml`), same | `MANDATORY_IE_INCORRECT` | `""` ("not a non-empty array") |
| `PATCH /3gpp-ims-sm/v1/ims-sessions/{sessionId}` (`TS29522_ImsSessionManagement.yaml`), same | `MANDATORY_IE_INCORRECT` | `""` ("not a non-empty array") |
| `POST /3gpp-as-session-with-qos/v1/{scsAsId}/subscriptions` (`TS29122_AsSessionWithQoS.yaml`), GPSI-only UE towards the TSCTSF without `qosReference` | `MANDATORY_IE_MISSING` | `/qosReference` ("required for a UE identified by GPSI only") |
| `POST /nnef-traffic-influence-data/v1/subscriptions` (`TS29591_Nnef_TrafficInfluenceData.yaml`), neither `dnns` nor `snssais` | `MANDATORY_IE_MISSING` | `/dnns` and `/snssais` ("absent; one of dnns and snssais is required") |
| `PUT /nnef-traffic-influence-data/v1/subscriptions/{subscriptionId}`, same | `MANDATORY_IE_MISSING` | `/dnns` and `/snssais` (same reason) |

- Every name was checked against the REL-19 YAML: `application-ids` is the AllFetch query
  parameter (`required: true`); `qosReference` is a member of `AsSessionWithQoSSubscription`;
  `dnns` and `snssais` are members of `TrafficInfluDataSub`, whose `anyOf: [{required: [dnns]},
  {required: [snssais]}]` is the rule the handler checks; the three PATCH bodies are declared as
  `application/json-patch+json`, an array of `PatchItem` with `minItems: 1`.
- The dnns/snssais pair comes from one helper, `dnns_or_snssais_missing()`, used by both the POST
  and the PUT. Both members are named because both are absent and either one would satisfy the
  `anyOf` (the UDR's `group-identifiers` site, ADR-0609, does the same).
- The JSON Patch sites name the whole body with the RFC 6901 pointer `""`, as the UDR does
  (ADR-0609): the body itself is the IE that is not a non-empty array.
- No status code, cause, title or detail changed. No route was added, removed or moved.
- Tests: `NefInvalidParams.*` in `tests/integration/test_nef_invalid_params.cpp`, one probe per
  kind (query parameter, header, body members, whole body), asserting the exact `param` and
  `reason`. No NEF NOTE 1 site names a path variable, so there is no path-variable probe. The
  probed routes were checked for shadowing (sbi-core takes the first matching route): none of
  them shares a method and a segment count with an earlier parameter route under the same API
  root.

**Rejected alternatives.**
- *Name the Content-Type header or the body at the two "Malformed multipart/related body" sites.*
  `sbi_core::multipart::parse` fails both for a Content-Type without a usable boundary and for a
  body that does not split on it, and the handler cannot tell which; naming one of them would be a
  guess (see Disclosed).
- *Fill invalidParams at the `dto_error_cause` sites from the nlohmann exception text.* Rejected in
  ADR-0607 for `parse_json_body`: the exception names the key without its nesting, so the pointer
  could be wrong.
- *Name only `/dnns` (or only `/snssais`) for the traffic influence data body.* Either member
  satisfies the `anyOf`; naming one would imply it is the required one.
- *Correct the Content-Type sites' cause while here.* A cause change is a behaviour change outside
  this step.

**Disclosed.**
- Two sites are left without invalidParams: the `MANDATORY_IE_INCORRECT` "Malformed
  multipart/related body" answers of Nnef_SMService SendSMS and Nnef_SMContext deliver
  (`main.cpp`, the `!parts.has_value() || parts->empty()` checks). See the rejected alternatives.
- The four NEF sites whose cause comes from `sbi_core::http2::dto_error_cause` (SendSMS and
  deliver jsonData parts, the 5GLAN parameter provisioning PATCH result, the monitoring event
  PATCH result) and the NEF's `parse_json_body` / `parse_multipart_json_body` callers keep
  answering `MANDATORY_IE_MISSING` / `MANDATORY_IE_INCORRECT` without invalidParams, the gap
  ADR-0607 disclosed for the parse helpers.
- The two Content-Type sites answer `MANDATORY_IE_MISSING` for a missing HTTP header. NOTE 1
  speaks of IEs, query parameters and 3gpp-Sbi-Discovery-* headers; Content-Type is none of the
  last, but `TS29571_CommonData.yaml` defines the `"header "` encoding of `param` for any header,
  so the header is named that way (the SEPP does the same for `3gpp-Sbi-Target-apiRoot`,
  ADR-0610). Whether a missing Content-Type should be this cause at all is not decided here.
- The `/qosReference` site is reached only when the NEF is configured with a TSCTSF base URL; the
  new integration test does not configure one, so that site is covered by the syntax check and
  review only.
- `MISSING_PARAMETER` (403): no NEF site answers it.

**Verified.** See the PR for what was checked on this machine: clang-format-18 and a syntax-only
compile of `nfs/nef/src/main.cpp` and the new test. No build, and the integration tests were not
run (they need the lab certificates, which this worktree does not read).

**Verified by the lead, 2026-11-23, main checkout (runner 23 idle).** Tree: this branch on origin/main 3837d995. Full build. The first run found a test bug: `SendSmsWithoutContentTypeNamesTheHeader` got `MANDATORY_IE_INCORRECT` 'Malformed multipart/related body', because the test's `send()` left the header out and libcurl then adds its own default `Content-Type: application/x-www-form-urlencoded` for a POST (`CURLOPT_POSTFIELDS`, `libs/sbi-core/src/http2_client.cpp`). Fixed in the test only: an empty content_type now sends `content-type:` with no value, which makes libcurl send no Content-Type. The NEF code was not changed. After the fix `NefInvalidParams.*` 4/4 pass. With `nfs/nef/src/main.cpp` restored to 3837d995 and rebuilt, all 4 fail. Regression: `--gtest_filter='Nef*'` 40/40 pass (171 s). Only these filters were run, not a full ctest.
