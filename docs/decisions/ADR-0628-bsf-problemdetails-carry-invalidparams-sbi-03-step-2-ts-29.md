## ADR-0628: BSF ProblemDetails carry invalidParams (SBI-03 step 2, TS 29.500 Table 5.2.7.2-1 NOTE 1)

**Date:** 2026-11-23. **Status:** Proposed.

**Context.** ADR-0607 (step 1) gave sbi-core a way to put `invalidParams` in a ProblemDetails and
left every NF unchanged; UDR (ADR-0609), SEPP (ADR-0610) and UDSF (ADR-0608) are step 2 for their
NFs. This is step 2 for the BSF (`nfs/bsf`, Nbsf_Management, `TS29521_Nbsf_Management.yaml`
REL-19 commit bca84b6). TS 29.500 V19.7.0 Table 5.2.7.2-1, NOTE 1: "\"invalidParams\" attribute
shall be included in the \"ProblemDetails\" data structure indicating unsupported, missing or
incorrect IE(s) or query parameter(s) or 3gpp-Sbi-Discovery-* header(s)." The NOTE 1 causes are
INVALID_QUERY_PARAM, MANDATORY_QUERY_PARAM_INCORRECT, OPTIONAL_QUERY_PARAM_INCORRECT,
MANDATORY_QUERY_PARAM_MISSING, MANDATORY_IE_INCORRECT, OPTIONAL_IE_INCORRECT, MANDATORY_IE_MISSING
(400) and MISSING_PARAMETER (403).

The BSF answers a NOTE 1 cause itself at two sites, both in `nfs/bsf/src/main.cpp`; every other
BSF error is 403 `EXISTING_BINDING_INFO_FOUND` (TS 29.521 table 5.7.3-1, not NOTE 1), a 403 with no
cause, 400 `INVALID_MSG_FORMAT` or a 404.

**Decision:** both sites carry `invalidParams` through the ADR-0607 overload and builders. Status
codes, causes, titles and details are unchanged.

| Site | Cause | `param` | `reason` |
| --- | --- | --- | --- |
| `POST /pcfBindings` (CreatePCFBinding), body has no `supi` | MANDATORY_IE_MISSING | `/supi` | `missing` |
| `GET /pcf-mbs-bindings` (GetPCFMbsBinding), `mbs-session-id` absent | MANDATORY_QUERY_PARAM_MISSING | `query mbs-session-id` | `missing` |
| same site, `mbs-session-id` present but not JSON | MANDATORY_QUERY_PARAM_MISSING | `query mbs-session-id` | `not JSON` |

- Names are the YAML's: `PcfBinding.supi`; the GetPCFMbsBinding query parameter `mbs-session-id`
  (`required: true`, `content: application/json`, schema `MbsSessionId`).
- Both sites are reachable. `PcfBinding` requires only `dnn` and `snssai`, so a body with those
  and no `supi` passes `parse_json_body` and reaches the supi check. The GET has no body. Each
  route is a distinct literal path; no earlier route shadows either.
- The mbs-session-id site's helper (`get_json_query_param`) returns nothing both when the
  parameter is absent and when its value does not parse as JSON. The handler now tells the two
  apart only to set `reason`; the cause stays as it was (see Disclosed).

Test: `BsfInvalidParams.*` (`tests/integration/test_bsf_invalid_params.cpp`, last in the
`integration_tests` source list): real NRF and BSF processes over TLS 1.3 + mTLS HTTP/2 with
NRF tokens; readiness by `nf_test::wait_until_ready` on `GET /pcf-ue-bindings` answering 200. The
BSF needs at startup: `config/bsf.json` (port 7789, metrics 9477), `certs/bsf`, `certs/ca`,
`certs/nrf-jwt/public.pem`, and an NRF (port 7777) for registration and the test's tokens. It keeps
all bindings and subscriptions in memory: no database, Valkey, Doris or peer NF.

**Rejected alternatives.**

- *Change the non-JSON mbs-session-id case to MANDATORY_QUERY_PARAM_INCORRECT.* Probably the
  better cause, but this step changes no cause; recorded in Disclosed for a later change.
- *Name nothing at the supi site because the YAML makes supi optional.* The NOTE 1 rule follows
  the cause the site sends; while it sends MANDATORY_IE_MISSING it must name the IE, and `/supi` is
  the IE the handler found missing.
- *Add invalidParams inside `parse_json_body`'s MANDATORY_IE_* answers here.* That is sbi-core's
  shared path, out of scope for an NF step per ADR-0607.

**Disclosed.**

- `/supi` with MANDATORY_IE_MISSING: `supi` is optional in `PcfBinding`; this BSF requires it for
  its duplicate-combination check (main.cpp simplification 1). The cause is unchanged and doubtful
  against "conditional IE but mandatory required"; TS 29.521's text was not in hand to settle it.
- The non-JSON `mbs-session-id` case still answers MANDATORY_QUERY_PARAM_MISSING although the
  parameter is present (MANDATORY_QUERY_PARAM_INCORRECT would describe it); only `reason` differs.
- `parse_json_body` failures (e.g. a PcfBinding without `dnn` or `snssai`, a PcfForUeBinding
  without `supi`) answer MANDATORY_IE_MISSING / MANDATORY_IE_INCORRECT from sbi-core without
  invalidParams (ADR-0607 scope). Not tested here.
- GetPCFBindings (`GET /pcfBindings`) never answers an error: all its query parameters are
  optional in the YAML and the handler ignores an unparseable `snssai` as if absent, and a call
  with no filter returns the first stored binding. No NOTE 1 site exists there, so nothing changes;
  whether TS 29.521 requires a parameter combination there was not checked against its text.
- GetPCFForUeBindings likewise has no error path. MISSING_PARAMETER (403) is never sent by the BSF.

**Verified (2026-11-24, worktree, no build).** `clang-format-18 --dry-run -Werror` on the two
changed C++ files; `-fsyntax-only` compiles of `nfs/bsf/src/main.cpp` and the new test with the
main checkout's `compile_commands.json`, both clean of errors. Not built, not run: the integration
test needs the lab certificates, not present in this worktree. CI result to be added.

**Verified by the lead, 2026-11-24, main checkout (runner 23 idle).** Tree: origin/main 4cb7a62e plus #79 (NSSF), #80 (BSF) and #81 (CHF) applied together; the only conflicts were the test files appended to the integration_tests list, resolved by keeping all three. Full build. `NssfInvalidParams.*` 4/4, `BsfInvalidParams.*` 3/3 and `ChfInvalidParams.*` 4/4 pass. With nfs/nssf, nfs/bsf and nfs/chf main.cpp restored to main and rebuilt, the 10 error-case tests fail; `NssfInvalidParams.BothPresentReachesTheSelection` is a positive control and passes either way. Regression: `--gtest_filter='Nssf*:Bsf*:Chf*'` 22/22 pass. Local Doris had chf_cdr applied. Only these filters were run, not a full ctest.
