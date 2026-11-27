## ADR-0639: AAnF ProblemDetails carry invalidParams (SBI-03 step 2, AAnF)

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
`MANDATORY_IE_MISSING` (400) and `MISSING_PARAMETER` (403). This ADR is step 2 for one NF, the AAnF
(`nfs/aanf`), in the shape of ADR-0634 (GMLC). With it, every NF in `nfs/` that answers a NOTE 1
cause has step 2 on main or in an open PR.

**Decision.** The AAnF's five NOTE 1 sites now send invalidParams. A grep of `nfs/aanf/src` for the
NOTE 1 causes finds five sites, all in `nfs/aanf/src/main.cpp` (`TS29535_Naanf_AKMA.yaml`; the
request schemas `AkmaKeyInfo` and `CtxRemove` there, `AkmaAfKeyRequest` in `TS29522_AKMA.yaml`):

| Route (operation) | Condition | Cause | invalidParams[].param |
|---|---|---|---|
| `POST /naanf-akma/v1/register-anchorkey` (AnchorKey_Register) | `kAkma` not 64 hex digits and/or `aKId` not an NAI | `MANDATORY_IE_INCORRECT` | `/kAkma` ("not 64 hexadecimal digits"), `/aKId` ("not an NAI"), each one that is wrong |
| same | no `supi`, or a `gpsi` | `MANDATORY_IE_MISSING` | `/supi` ("absent; this AAnF registers by SUPI") when absent; `/gpsi` ("not supported; this AAnF registers by SUPI") when given |
| same | `supi` off the TS 29.571 `Supi` pattern | `MANDATORY_IE_INCORRECT` | `/supi` ("does not match the Supi pattern") |
| `POST /naanf-akma/v1/retrieve-applicationkey` (ApplicationKey_Get) | malformed `afId` | `MANDATORY_IE_INCORRECT` | `/afId` |
| `POST /naanf-akma/v1/remove-context` (Context_Remove) | no `supi` | `MANDATORY_IE_MISSING` | `/supi` |

- The local `problem` helper gains an overload that takes the `InvalidParam` list.
- No status code, cause, title or detail changed.
- Test: `AanfInvalidParams.*` in `tests/integration/test_aanf_invalid_params.cpp` (target `akma_e2e_integration_tests`, beside `test_aanf_authorization.cpp`; real nrf + aanf
  processes, TLS 1.3 + mTLS HTTP/2, a real NRF token for scope `naanf-akma`; register and remove
  sent with the AUSF's certificate, SEC-03): one test per site, each asserting the exact cause and
  the exact invalidParams list, and `ValidRegistrationIsAccepted` (200, then removes the context
  with 204), a positive control. The tests use their own A-KID (`invalid-params@akma.example`):
  Valkey outlives the processes, and `test_aanf_authorization.cpp` expects no context for
  `x@akma.example`.

**Rejected alternatives.**
- Naming only `/supi` when a `gpsi` is given: the `gpsi` is what this AAnF does not accept, so it is
  named too.
- Changing the causes: step 2 changes no status or cause; see Disclosed.

**Disclosed.**
- `AkmaKeyInfo` is `oneOf: [required: [supi], required: [gpsi]]` in the YAML; registering by GPSI
  is valid there but not accepted by this AAnF (it keys contexts by SUPI, TS 33.535 6.6), as the
  handler's comment already says. A request with only a `gpsi` is answered `MANDATORY_IE_MISSING`
  naming both IEs; whether the GPSI branch should instead be accepted or answered with another
  cause is left for a decision of its own.
- `TS29571_CommonData.yaml` `Supi` is `'^(imsi-[0-9]{5,15}|nai-.+|gci-.+|gli-.+|.+)$'`; its last
  branch takes any non-empty string, so only an empty value or one with a line terminator (which
  `supi_conforms` also refuses, ADR-0549) is off the pattern. The test uses an empty `supi`.
- DTO parse failures are answered `INVALID_MSG_FORMAT` by this NF (not a NOTE 1 cause) and are
  unchanged. The 401/403 answers are not NOTE 1 causes. Errors answered by sbi-core before the
  handler are not part of this step.

**Verified, 2026-11-26, main checkout.** Tree: origin/main 9022f3a6 plus this change. The local
`certs/aanf/kakma.kek` was missing; it was made with `scripts/gen-lab-pki.sh aanf` (which creates
only absent files), with the user's approval. clang-format-18 on the changed C++ files.
`cmake --build build --target akma_e2e_integration_tests`, 0 failures.
`--gtest_filter='AanfInvalidParams.*:AanfAuthorization.*'` 8/8 pass, also twice more with
`--gtest_shuffle --gtest_repeat=2`. An earlier version of the positive control used
`x@akma.example` and left its context in Valkey, which made
`AanfAuthorization.OnlyTheAusfRegistersOrRemovesAndAnAfGetsOnlyItsOwnKaf` fail on the next run; the
test now uses its own A-KID and removes its context, and that leftover was cleared. With
`nfs/aanf/src/main.cpp` restored to origin/main and rebuilt, the five error-case tests fail (each
with invalidParams `{}`) and `ValidRegistrationIsAccepted` passes; the fix was then restored and
rebuilt. The master runner was idle (runner API `busy: false`). Only these filters were run, not a
full ctest.
