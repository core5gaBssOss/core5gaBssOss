## ADR-0537: AAnF roaming check fails closed when the UDM cannot answer (SEC-06)

**Date:** 2026-10-30. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SEC-06 (Medium). With `deny_roaming_ues` on, the AAnF asks the UDM
for the UE's roaming status (ADR-0497, TS 33.535 6.2.1 steps 5-7) before it gives an AF a KAF.
Any failure left the status unknown, and AKMA was provided:

- no answer, a timeout, a status other than 201, or a body that is not an EeSubscription;
- the failure was only logged.

A UDM outage therefore granted AKMA to roaming UEs. ADR-0497 disclosed only the "UE not
registered, no report" case.

**Decision.** When the UDM call itself fails, the AAnF refuses AKMA by default.

- **Failure** means: no answer (connection refused, timeout), any status other than 201, or a 201
  whose body is not a JSON object.
- **Not a failure:** a 201 EeSubscription without a ROAMING_STATUS report (UE not registered).
  AKMA is still provided and logged, as ADR-0497 discloses.
- **Answer:** 500 `SYSTEM_FAILURE` (TS 29.500 Table 5.2.7.2-1, "generic error condition in the
  NF"), with the UDM's failure in `detail`.
  - `ROAMING_AKMA_SERVICE_DENIED` is not used. TS 29.535 5.1.7.3 defines it for a request "for a
    roaming UE", and this UE is not identified as roaming.
  - 504 (`TARGET_NF_NOT_REACHABLE`, `TIMED_OUT_REQUEST`) is not used either:
    `TS29535_Naanf_AKMA.yaml` GetAKMAAPPKeyMaterial lists 500, 502 and 503, not 504.
- **Operator choice:** `roaming_check_on_udm_failure` in `config/aanf.json`, `"deny"` (default) or
  `"provide"`. `"provide"` keeps the old fail-open behaviour as a disclosed policy. Any other value
  stops the AAnF at startup. The key is in the GUI config schema, the compose lab config and the
  Helm chart config (all regenerated, not hand-written).

**Disclosed.**

- **The AAnF refuses AKMA for every UE while the UDM is down** (with `deny_roaming_ues` on and the
  default `deny`). That is the intended fail-closed trade-off; `"provide"` is the escape hatch.
- **No new cause.** 500 `SYSTEM_FAILURE` is generic. An AF cannot tell a UDM outage from another
  internal failure, except from `detail`.
- **Unchanged:** `deny_roaming_ues` off means no UDM call and no refusal, as before.

**Rejected alternatives.**

- **403 `ROAMING_AKMA_SERVICE_DENIED` on a UDM failure.** It would state that the UE roams, which
  nobody has established.
- **504 `TARGET_NF_NOT_REACHABLE`.** The YAML does not list 504 for this operation.
- **Fail closed with no configuration.** CLAUDE.md guardrail 8 (configuration over code), and the
  review's fix hint ("fail closed (or configurably)").

**Verified (2026-10-30).**

- **Spec texts read:** TS 29.535 j50 4.2.2.3.2 and 5.1.7.3; TS 29.500 j70 Table 5.2.7.2-1; YAML
  `TS29535_Naanf_AKMA.yaml` GetAKMAAPPKeyMaterial responses.
- **Syntax:** `g++-15 -fsyntax-only` with the build's flags on `nfs/aanf/src/main.cpp` and
  `tests/integration/test_akma_e2e.cpp`. No errors.
- **Formatting:** `clang-format-18` applied.
- **Generated files:** regenerated with `gen_compose_config.py`, `gen_helm_config.py` and
  `derive_nf_config_schemas.py`.
- **Test added but not run here** (the CI runner shares this host and was busy):
  `AkmaEndToEnd.AusfRegistersKakmaAndTheAanfGivesTheAfTheSameKafTheUeDerives` now stops the UDM
  and expects 500 `SYSTEM_FAILURE` and no `kaf`. The old code answered 200 with a KAF there.
