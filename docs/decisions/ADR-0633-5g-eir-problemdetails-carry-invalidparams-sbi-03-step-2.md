## ADR-0633: 5G-EIR ProblemDetails carry invalidParams (SBI-03 step 2)

**Date:** 2026-11-24. **Status:** Proposed.

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
5G-EIR (`nfs/eir`), in the shape of ADR-0609 (UDR) and ADR-0629 (NSSF, the query-parameter case).

**Decision.** The EIR's one NOTE 1 site now sends invalidParams. A grep of `nfs/eir` on this
branch finds two `problem_response` calls, both in `nfs/eir/src/main.cpp` (none in `stores.cpp` /
`stores.hpp`), and one of them answers a NOTE 1 cause:

| Route (operation) | Cause | invalidParams[].param |
|---|---|---|
| `GET /n5g-eir-eic/v1/equipment-status` (GetEquipmentStatus), no `pei` | `MANDATORY_QUERY_PARAM_MISSING` | `query pei` (`required: true` in `TS29511_N5g-eir_EquipmentIdentityCheck.yaml`) |

- Built with `sbi_core::invalid_query_param("pei")`; no reason is given (the cause already says
  "missing").
- No status code, cause, title or detail changed. The other EIR error, the ADR-0597 404
  `ERROR_EQUIPMENT_UNKNOWN` for a PEI with no status, is not a NOTE 1 cause and is unchanged.
- Reachability: the EIR registers one route (exact path, no wildcard), so nothing shadows it; the
  check is the handler's first statement after the Authorizer has accepted the token.
- Test: `EirInvalidParams.*` in `tests/integration/test_eir_invalid_params.cpp` (real nrf + eir
  processes, TLS 1.3 + mTLS HTTP/2, a real NRF token for scope `n5g-eir-eic`):
  `NoQueryParametersNamesPei`, `OnlyOptionalParametersStillNamesPei` (supi + gpsi, no pei),
  `PeiPresentReachesTheStatusLookup` (200 for a seeded PEI). Each asserts the exact cause and the
  exact invalidParams list. Startup needs: `config/eir.json` (port 7792, metrics 0.0.0.0:9480,
  nrf_base_url 127.0.0.1:7777), `certs/eir/{cert,key}.pem`, `certs/ca/ca.crt`,
  `certs/nrf-jwt/public.pem`, and the NRF on 7777 (the EIR registers with it; the token is issued
  only once it has).

**Rejected alternatives.**
- Also validating `supi` / `gpsi` / `supported-features` and answering
  `OPTIONAL_QUERY_PARAM_INCORRECT` with invalidParams: that adds new error behaviour, not
  invalidParams on an existing error. Out of scope for step 2.
- Answering an empty `pei` (`?pei=` or bare `?pei`) as `MANDATORY_QUERY_PARAM_INCORRECT` with
  `query pei`: a status/cause change, which this step does not make (see Disclosed).

**Disclosed.**
- An empty `pei` value is present to the handler (sbi-core's `parse_query_string` stores a bare
  `pei` as an empty value), so it falls through to the store lookup and is answered 404
  `ERROR_EQUIPMENT_UNKNOWN`, without invalidParams. Whether an empty or pattern-violating PEI
  (`TS29571_CommonData.yaml` `Pei`) should instead be `MANDATORY_QUERY_PARAM_INCORRECT` is left
  for a decision of its own.
- The optional `supi`, `gpsi` and `supported-features` query parameters are read by nothing and
  never refused, so no `OPTIONAL_QUERY_PARAM_INCORRECT` site exists in the EIR.
- The EIR reads no request body and no `3gpp-Sbi-Discovery-*` header, so no JSON Pointer or
  `header ...` entry arises. Errors answered by sbi-core itself before the handler (Authorizer,
  TPS limit, routing) are sbi-core's, not this NF's, and are not part of this step.

**Verified.** clang-format-18 `--dry-run -Werror` on the changed C++ files, and a syntax-only
compile (`-fsyntax-only`, flags from the main checkout's `compile_commands.json`) of
`nfs/eir/src/main.cpp` and `tests/integration/test_eir_invalid_params.cpp`. Not built, not run:
the integration tests need the lab certificates, which this worktree does not read; the lead runs
them in the main checkout.

**Verified by the lead, 2026-11-24, main checkout.** Tree: origin/main 4cb7a62e plus #82 (EIR) and #83 (SMSF) applied together; the only conflict was the two test files appended to the integration_tests list, resolved by keeping both. Full build. `EirInvalidParams.*` 3/3 and `SmsfInvalidParams.*` 2/2 pass. With nfs/eir/src/main.cpp and nfs/smsf/src/main.cpp restored to main and rebuilt, the 4 error-case tests fail; `EirInvalidParams.PeiPresentReachesTheStatusLookup` is a positive control and passes either way. Regression: `--gtest_filter='Eir*:Smsf*'` 8/8 pass. The master runner had no Runner.Worker process during the runs (GitHub's runner API was returning HTTP 500). Only these filters were run, not a full ctest.
