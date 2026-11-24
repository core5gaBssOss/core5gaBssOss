## ADR-0632: SMSF ProblemDetails carry invalidParams (SBI-03 step 2, SMSF)

**Date:** 2026-11-24. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that takes an
`InvalidParam` list and four builders for the `param` encodings of `TS29571_CommonData.yaml`
(`InvalidParam.param`: a JSON Pointer for a body attribute, `"query "` plus the name for a query
parameter, `"header "` plus the name for a header, `{name}` for a path variable). TS 29.500
V19.7.0, Table 5.2.7.2-1, NOTE 1: "\"invalidParams\" attribute shall be included in the
\"ProblemDetails\" data structure indicating unsupported, missing or incorrect IE(s) or query
parameter(s) or 3gpp-Sbi-Discovery-* header(s)." The NOTE 1 causes are `INVALID_QUERY_PARAM`,
`MANDATORY_QUERY_PARAM_INCORRECT`, `OPTIONAL_QUERY_PARAM_INCORRECT`,
`MANDATORY_QUERY_PARAM_MISSING`, `MANDATORY_IE_INCORRECT`, `OPTIONAL_IE_INCORRECT`,
`MANDATORY_IE_MISSING` (400) and `MISSING_PARAMETER` (403). The UDR (ADR-0609), SEPP (ADR-0610),
UDSF (ADR-0608) and NRF (ADR-0613) are done; this ADR is step 2 for the SMSF (`nfs/smsf`), whose
routes all come from `TS29540_Nsmsf_SMService.yaml` (SendMtSMS's JSON part is `SmsData` from
`TS29577_Nipsmgw_SMService.yaml`).

**Decision.** A count on this branch finds 7 SMSF sites that answer a NOTE 1 cause, all in
`nfs/smsf/src/main.cpp` (none in `stores.cpp`, `mt_sms.cpp`, `n1n2_sender.cpp`,
`sms_procedures.cpp`, `li_poi.cpp` or the headers). The 2 that can name the parameter honestly now
send invalidParams:

| Route (operation) | Cause | invalidParams[].param (reason) |
|---|---|---|
| `POST {apiRoot}/ue-contexts/{supi}/sendsms` (SendSMS) without a Content-Type | `MANDATORY_IE_MISSING` | `header Content-Type` ("missing") |
| `POST {apiRoot}/ue-contexts/{supi}/send-mt-sms` (SendMtSMS) without a Content-Type | `MANDATORY_IE_MISSING` | `header Content-Type` ("missing") |

- Both request bodies are declared `multipart/related` in `TS29540_Nsmsf_SMService.yaml`; the
  header is named with sbi-core's `invalid_header`, as the NEF does for its own SMS routes
  (ADR-0623).
- No status code, cause, title or detail changed. No route was added, removed or moved.
- Reachability: both handlers look the UE SMS context up first and answer 404 `CONTEXT_NOT_FOUND`
  before the Content-Type check, so the sites are reached only for an activated SUPI. Route order
  was checked for shadowing (sbi-core takes the first matching route): the five SMSF routes are
  `PUT`/`PATCH`/`DELETE /ue-contexts/{supi}` and `POST .../{supi}/sendsms`,
  `POST .../{supi}/send-mt-sms`; the two POSTs differ in a literal segment and nothing else shares
  their method and segment count.
- Tests: `SmsfInvalidParams.*` in `tests/integration/test_smsf_invalid_params.cpp` (registered last
  in `integration_tests`). Each test spawns a real `nrf` and `smsf` (the SMSF needs
  `config/smsf.json`, its `certs/smsf` and `certs/nrf-jwt/public.pem`, the CA, and the NRF on
  127.0.0.1:7777 for tokens; no AMF is needed for these probes, as both answers come before the
  Namf calls), waits with `nf_test::wait_until_ready` for a 404 on SMSServiceParameterUpdate,
  activates a context with SMServiceActivation (201), then POSTs with no Content-Type (an empty
  `content-type` value, so libcurl adds no default) and asserts the exact cause and
  invalidParams.

**Rejected alternatives.**
- *Name the Content-Type header or the body at the two "Malformed multipart/related body" sites.*
  `sbi_core::multipart::parse` fails both for a Content-Type without a usable boundary and for a
  body that does not split on it, and the handler cannot tell which; naming one would be a guess
  (as ADR-0623 decided for the NEF).
- *Fill invalidParams at the `dto_error_cause` sites from the nlohmann exception text.* Rejected in
  ADR-0607: the exception names the key without its nesting, so the pointer could be wrong.
- *Correct the Content-Type sites' cause while here.* A cause change is a behaviour change outside
  this step.

**Disclosed.**
- Five sites are left without invalidParams:
  - the two `MANDATORY_IE_INCORRECT` "Malformed multipart/related body" answers (SendSMS and
    SendMtSMS, the `!parts.has_value() || parts->empty()` checks); see the rejected alternatives;
  - the two jsonData-part reads whose cause comes from `sbi_core::http2::dto_error_cause`
    (`SmsRecordData` for SendSMS, `SmsData` for SendMtSMS), `MANDATORY_IE_MISSING` /
    `MANDATORY_IE_INCORRECT`;
  - SMServiceActivation's `parse_json_body<UeSmsContextData>`, the same sbi-core gap ADR-0607
    disclosed for the parse helpers.
- The two Content-Type sites answer `MANDATORY_IE_MISSING` for a missing HTTP header. NOTE 1
  speaks of IEs, query parameters and 3gpp-Sbi-Discovery-* headers; Content-Type is none of the
  last, but `TS29571_CommonData.yaml` defines the `"header "` encoding of `param` for any header,
  so the header is named that way (as ADR-0610 and ADR-0623 do). Whether a missing Content-Type
  should be this cause at all is not decided here.
- Not NOTE 1 and unchanged: SendSMS's "smsPayload binary part not found" and "not an SM-CP
  message" answers use `UNSPECIFIED_MSG_FAILURE`; SendMtSMS's use the API causes
  `SMS_PAYLOAD_MISSING` / `SMS_PAYLOAD_ERROR`. SMSServiceParameterUpdate's malformed JSON is
  `INVALID_MSG_FORMAT`.
- `MISSING_PARAMETER` (403): no SMSF site answers it. No SMSF NOTE 1 site concerns a query
  parameter or a path variable.

**Verified.** See the PR for what was checked on this machine: clang-format-18 and a syntax-only
compile of `nfs/smsf/src/main.cpp` and the new test. No build, and the integration tests were not
run (they need the lab certificates, which this worktree does not read).
