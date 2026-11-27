## ADR-0638: TSCTSF ProblemDetails carry invalidParams (SBI-03 step 2, TSCTSF)

**Date:** 2026-11-26. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that takes an
`InvalidParam` list and builders for the `param` encodings of `TS29571_CommonData.yaml`
(`InvalidParam.param`: a JSON Pointer for a body attribute, `"query "` plus the name for a query
parameter, `"header "` plus the name for a header, `{name}` for a path variable). TS 29.500
V19.7.0 clause 5.2.7.2, Table 5.2.7.2-1 NOTE 1: "\"invalidParams\" attribute shall be included in
the \"ProblemDetails\" data structure indicating unsupported, missing or incorrect IE(s) or query
parameter(s) or 3gpp-Sbi-Discovery-* header(s)." The NOTE 1 causes are `INVALID_QUERY_PARAM`,
`MANDATORY_QUERY_PARAM_INCORRECT`, `OPTIONAL_QUERY_PARAM_INCORRECT`,
`MANDATORY_QUERY_PARAM_MISSING`, `MANDATORY_IE_INCORRECT`, `OPTIONAL_IE_INCORRECT`,
`MANDATORY_IE_MISSING` (400) and `MISSING_PARAMETER` (403). This ADR is step 2 for one NF, the
TSCTSF (`nfs/tsctsf`), in the shape of ADR-0634 (GMLC).

**Decision.** The TSCTSF's four NOTE 1 sites now send invalidParams. A grep of `nfs/tsctsf/src` for
the NOTE 1 causes finds four sites, all on `POST /ntsctsf-qos-tscai/v1/tsc-app-sessions` (Create,
`TscAppSessionContextData` in `TS29565_Ntsctsf_QoSandTSCAssistance.yaml`) in
`nfs/tsctsf/src/main.cpp`:

| Condition | Cause | invalidParams[].param |
|---|---|---|
| `qosReference` empty and no `tscQosReq` | `MANDATORY_IE_MISSING` | `/qosReference`, reason "empty" (an absent one fails the DTO read first) |
| any of `externalGroupId`, `ueMac`, `ethFlowInfo`, `enEthFlowInfo` present | `OPTIONAL_IE_INCORRECT` | each one present, reason "not supported by this TSCTSF" |
| `ueId` without the GMEC feature in `suppFeat` | `MANDATORY_IE_INCORRECT` | `/ueId`, reason "requires the GMEC feature" |
| no usable target UE | `MANDATORY_IE_MISSING` | `/ueIpAddr/ipv4Addr` ("absent; required by this TSCTSF") when `ueIpAddr` is present without it; otherwise `/ueIpAddr` and `/ueId` ("absent; one of ueIpAddr and ueId is required") |

- The local `problem` helper gains an overload that takes the `InvalidParam` list.
- No status code, cause, title or detail changed.
- Test: `TsctsfInvalidParams.*` in `tests/integration/test_tsctsf_invalid_params.cpp` (real nrf +
  tsctsf processes, TLS 1.3 + mTLS HTTP/2, a real NRF token for scope `ntsctsf-qos-tscai`): one
  test per condition (five, since the target-UE site has two branches), each asserting the exact
  cause and the exact invalidParams list, and `ValidRequestPassesTheChecks`, a positive control: a
  valid request reaches the BSF lookup and, with no BSF running, is answered 500
  `PDU_SESSION_NOT_AVAILABLE`.

**Rejected alternatives.**
- Naming `/suppFeat` for the GMEC site: the request's `suppFeat` is not wrong in itself; it is the
  `ueId` that cannot be accepted without the feature.
- Changing the causes: step 2 changes no status or cause; see Disclosed.

**Disclosed.**
- An empty `qosReference` is present but answered `MANDATORY_IE_MISSING`; whether it should be
  `MANDATORY_IE_INCORRECT` is left for a decision of its own. The YAML requires `qosReference`;
  this TSCTSF also accepts its absence's stand-in, `tscQosReq`, as before.
- The unsupported-IE site names IEs that are valid per the YAML but not implemented here (external
  groups and Ethernet PDU sessions), as the existing detail already says.
- The target-UE `/ueIpAddr/ipv4Addr` entry reflects this TSCTSF's IPv4-only BSF lookup, not a
  YAML requirement.
- DTO parse failures are answered `INVALID_MSG_FORMAT` by this NF (not a NOTE 1 cause) and are
  unchanged. Errors answered by sbi-core before the handler are not part of this step.

**Verified, 2026-11-26, main checkout.** Tree: origin/main 9022f3a6 plus this change.
clang-format-18 on the changed C++ files. `cmake --build build --target integration_tests`, 0
failures. `--gtest_filter='TsctsfInvalidParams.*:*TsctsfAuthorization*'` 7/7 pass. With
`nfs/tsctsf/src/main.cpp` restored to origin/main and rebuilt, the five error-case tests fail (each
with invalidParams `{}`) and `ValidRequestPassesTheChecks`, the positive control, passes; the fix
was then restored and rebuilt. The master runner was idle (runner API `busy: false`). Only these
filters were run, not a full ctest.
