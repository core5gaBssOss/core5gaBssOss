## ADR-0627: AUSF ProblemDetails carry invalidParams (SBI-03 step 2, AUSF)

**Date:** 2026-11-23. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that takes an
`InvalidParam` list and builders for the `param` encodings of `TS29571_CommonData.yaml` (a JSON
Pointer for a body attribute, `"header "` / `"query "` plus the name, `{name}` for a path
variable). TS 29.500 V19.7.0 Table 5.2.7.2-1 NOTE 1: "\"invalidParams\" attribute shall be
included in the \"ProblemDetails\" data structure indicating unsupported, missing or incorrect
IE(s) or query parameter(s) or 3gpp-Sbi-Discovery-* header(s)." This ADR is step 2 for one NF,
the AUSF (`nfs/ausf`), after the UDR (ADR-0609), SEPP (ADR-0610) and UDSF (ADR-0608).

**Decision.** A count of every `problem_response` in `nfs/ausf/src/` (`main.cpp`; `stores.cpp`
and `kausf_store.cpp` send none) finds exactly one site that answers a NOTE 1 cause itself:

| Route (handler) | Cause | invalidParams[].param |
|---|---|---|
| Nausf_SoRProtection `POST /nausf-sorprotection/v1/{supi}/ue-sor`, request without `sorHeader` | `MANDATORY_IE_MISSING` (400) | `/sorHeader` (reason: "absent; this build does not construct the SOR header itself") |

`sorHeader` is the `SorInfo` member of that name in `TS29509_Nausf_SoRProtection.yaml`. No status,
cause, title or detail changed. Every other AUSF error answers a cause outside NOTE 1
(`SYSTEM_FAILURE`, `CONTEXT_NOT_FOUND`, `USER_NOT_FOUND`, `UNSPECIFIED_MSG_FAILURE`,
`COUNTER_WRAP`) or no cause at all (the 404s for a SUPI without a KAUSF, the 501s). The AUSF
does not relay a UDM ProblemDetails: a non-200, non-404 answer from the UDM becomes the AUSF's own
500 `SYSTEM_FAILURE`, so the UDM's supiOrSuci cases (ADR-0614) do not reach the AUSF's consumer
and the ADR-0603 supiOrSuci encoding toward the UDM is untouched.

**Reachability.** `SorInfo` requires only `ackInd`, so a body `{"ackInd": false}` passes
`parse_json_body`. The `sorHeader` check runs after the KAUSF lookup, so the SUPI must first
complete a primary authentication; the test runs a real 5G-AKA first. The SoR route has its own
API root and no other route can shadow it.

**Test.** `AusfInvalidParams.SorProtectionWithoutSorHeaderNamesSorHeader`
(`tests/integration/test_ausf_invalid_params.cpp`): real nrf + udm + udr + ausf, 5G-AKA for
`imsi-999700000000001`, then the SoR request without `sorHeader`; asserts 400,
`MANDATORY_IE_MISSING` and invalidParams `["/sorHeader"]` with a reason. Needs the lab
certificates, Valkey at the AUSF's `redis_url` and the UDR's Postgres.

**Out of scope, unchanged.** The AUSF's `parse_json_body` sites (400 `MANDATORY_IE_MISSING` /
`MANDATORY_IE_INCORRECT` from a generated `from_json`) still send no invalidParams, as ADR-0607
disclosed: the nlohmann exception gives no JSON Pointer.

**Disclosed.**
- The cause is doubtful. `sorHeader` is optional in `SorInfo` (only `ackInd` is required), and
  the handler's own comment cites TS 33.501 clause 6.14.2 NOTE 2 as letting the AUSF build the
  SOR header itself when the requester does not send one (not re-checked against TS 33.501 for
  this ADR; that spec is not in hand here). The AUSF answers `MANDATORY_IE_MISSING` because this
  build does not implement that construction (TS 24.501 9.11.3.51 encoding). The task was not to
  change causes; it stays as it was, for a decision of its own.
- `UNSPECIFIED_MSG_FAILURE` sites (bad base64 in `sorHeader` / `steeringContainer` / `nonce1`, bad
  hex `resStar`, empty `upuDataList`) are outside NOTE 1 and send no invalidParams, though each
  names one IE; a cause such as `MANDATORY_IE_INCORRECT` might fit some of them better. Not
  changed here.

**Verified.** clang-format-18 and a syntax-only compile of the two changed translation units. The
integration test was not built or run in this worktree: it needs the lab certificates.

**Rejected alternatives.**
- *Change the cause to one that fits an optional IE.* A cause change is a behaviour change
  outside this step.
- *Attach invalidParams to the `UNSPECIFIED_MSG_FAILURE` sites as well.* NOTE 1 does not cover
  that cause; adding it there would be a step beyond the table this ADR implements.
