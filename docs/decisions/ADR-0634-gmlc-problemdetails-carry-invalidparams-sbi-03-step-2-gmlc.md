## ADR-0634: GMLC ProblemDetails carry invalidParams (SBI-03 step 2, GMLC)

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
GMLC (`nfs/gmlc`), in the shape of ADR-0633 (5G-EIR) and of ADR-0623 (NEF) for the "one of two IEs
is required" case.

**Decision.** The GMLC's one NOTE 1 site now sends invalidParams. A grep of `nfs/gmlc/src` for the
NOTE 1 causes finds one site, in `nfs/gmlc/src/main.cpp`:

| Route (operation) | Cause | invalidParams[].param |
|---|---|---|
| `POST /ngmlc-loc/v1/location-update` (LocationUpdate), neither `supi` nor `gpsi` in `LocUpdateData` | `MANDATORY_IE_MISSING` | `/supi` and `/gpsi`, each with reason "absent; one of supi and gpsi is required" |

- Built with `sbi_core::invalid_body_ie`. Both IEs are named because either one would satisfy the
  check, as ADR-0623 does for the NEF's `dnns` / `snssais`.
- No status code, cause, title or detail changed.
- Test: `GmlcInvalidParams.*` in `tests/integration/test_gmlc_invalid_params.cpp` (real nrf + gmlc
  processes, TLS 1.3 + mTLS HTTP/2, a real NRF token for scope `ngmlc-loc`):
  `NeitherSupiNorGpsiNamesBoth` (exact cause and exact invalidParams list) and
  `SupiPresentIsStored` (204, a positive control). The body carries every IE that
  `TS29515_Ngmlc_Location.yaml` marks required in `LocUpdateData`.

**Rejected alternatives.**
- Naming only `/supi`: it would tell the consumer that the SUPI is mandatory, which it is not;
  a GPSI alone is accepted.
- Changing the cause: see Disclosed. Step 2 adds invalidParams to existing errors and changes no
  status or cause.

**Disclosed.**
- `TS29515_Ngmlc_Location.yaml` requires neither `supi` nor `gpsi` in `LocUpdateData`. The 400 is
  this project's own store-key need, already disclosed in the handler's comment. It
  stays `MANDATORY_IE_MISSING` here; whether a spec-optional IE should be answered with that cause
  at all is left for a decision of its own.
- The other two GMLC errors, 403 `LOCATION_SESSION_UNKNOWN` on CancelLocation and the generated-DTO
  parse failures (`INVALID_MSG_FORMAT`), are not NOTE 1 causes and are unchanged. Errors answered by
  sbi-core itself before the handler (Authorizer, TPS limit, routing) are not part of this step.
- Of the NFs without step 2 on main or in an open PR, DCCF and SCP answer only
  `INVALID_MSG_FORMAT`, and UPF answers no NOTE 1 cause, so they need no step 2 change. AMF, LMF,
  MFAF, TSCTSF and AANF still have NOTE 1 sites without invalidParams.

**Verified, 2026-11-26, main checkout.** Tree: origin/main 9022f3a6 plus this change.
clang-format-18 on the changed C++ files. `cmake --build build --target integration_tests`, 0
failures. `--gtest_filter='Gmlc*'` 4/4 pass (`GmlcAuthorization.*` 2, `GmlcInvalidParams.*` 2). With
`nfs/gmlc/src/main.cpp` restored to origin/main and rebuilt, `NeitherSupiNorGpsiNamesBoth` fails
(invalidParams `{}`) and `SupiPresentIsStored`, the positive control, passes; the fix was then
restored and rebuilt. The master runner was idle (runner API `busy: false`). Only these filters were
run, not a full ctest.
