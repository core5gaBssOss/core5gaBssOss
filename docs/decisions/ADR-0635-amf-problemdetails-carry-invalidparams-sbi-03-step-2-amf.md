## ADR-0635: AMF ProblemDetails carry invalidParams (SBI-03 step 2, AMF)

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
`MANDATORY_IE_MISSING` (400) and `MISSING_PARAMETER` (403). This ADR is step 2 for one NF, the AMF
(`nfs/amf`), in the shape of ADR-0634 (GMLC).

**Decision.** The AMF's one NOTE 1 site now sends invalidParams. A grep of `nfs/amf/src` for the
NOTE 1 causes finds one site, in `nfs/amf/src/main.cpp`:

| Route (operation) | Cause | invalidParams[].param |
|---|---|---|
| `POST /namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages` (N1N2MessageTransfer), a non-SMS N1 message without `pduSessionId` | `MANDATORY_IE_MISSING` | `/pduSessionId` |

- Built with `sbi_core::invalid_body_ie`; no reason is given (the cause already says "missing").
- No status code, cause, title or detail changed.
- Test: `AmfInvalidParams.*` in `tests/integration/test_amf_invalid_params.cpp` (real nrf + amf
  processes, TLS 1.3 + mTLS HTTP/2, a real NRF token for scope `namf-comm`; the UE context is made
  first with a multipart CreateUEContext): `SmTransferWithoutPduSessionIdNamesIt` (exact cause and
  exact invalidParams list) and `PduSessionIdPresentPassesTheCheck` (with `pduSessionId` and
  `skipInd`, 200 `N1_MSG_NOT_TRANSFERRED` for a UE with no NGAP association, ADR-0523), a positive
  control.

**Rejected alternatives.**
- Changing the cause: see Disclosed. Step 2 adds invalidParams to existing errors and changes no
  status or cause.

**Disclosed.**
- `TS29518_Namf_Communication.yaml` does not list `pduSessionId` as required in
  `N1N2MessageTransferReqData`. The 400 is this build's own routing need, already disclosed in the
  detail ("This build requires pduSessionId ..."). It stays `MANDATORY_IE_MISSING` here; whether a
  spec-optional IE should be answered with that cause is left for a decision of its own, as for the
  GMLC (ADR-0634).
- The AMF's two `UNSPECIFIED_MSG_FAILURE` answers are not NOTE 1 causes and are unchanged; nor are
  the multipart and DTO parse failures (`INVALID_MSG_FORMAT`). Errors answered by sbi-core itself
  before the handler (Authorizer, TPS limit, routing) are not part of this step.

**Verified, 2026-11-26, main checkout.** Tree: origin/main 9022f3a6 plus this change.
clang-format-18 on the changed C++ files. `cmake --build build --target integration_tests`, 0
failures. The AMF needs Valkey on 6379 at startup (the local `valkey-local` container was started for
the run). `--gtest_filter='AmfInvalidParams.*:AmfIntegration.*'` 7/7 pass. With
`nfs/amf/src/main.cpp` restored to origin/main and rebuilt, `SmTransferWithoutPduSessionIdNamesIt`
fails (invalidParams `{}`) and `PduSessionIdPresentPassesTheCheck`, the positive control, passes;
the fix was then restored and rebuilt. The master runner was idle (runner API `busy: false`). Only
these filters were run, not a full ctest.
