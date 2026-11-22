## ADR-0614: UDM ProblemDetails carry invalidParams (SBI-03 step 2, UDM)

**Date:** 2026-11-22. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that takes an
`InvalidParam` list and four builders for the `param` encodings of `TS29571_CommonData.yaml`
(`InvalidParam.param`: a JSON Pointer for a body attribute, `"query "` plus the name for a query
parameter, `"header "` plus the name for a header, `{name}` for a path variable). TS 29.500
V19.7.0 Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the
'ProblemDetails' data structure indicating unsupported, missing or incorrect IE(s) or query
parameter(s) or 3gpp-Sbi-Discovery-* header(s)." The same table gives `MANDATORY_IE_INCORRECT`
as "A mandatory IE (within the JSON body or within a variable part of an
"apiSpecificResourceUriPart" or within an HTTP header) ... was received with a semantically
incorrect value. (NOTE 1)", so a path variable is a NOTE 1 IE as well. ADR-0609 (UDR), ADR-0610
(SEPP) and ADR-0608 (UDSF) did step 2 for their NFs; this ADR is step 2 for the UDM
(`nfs/udm`).

**Decision:** Every UDM site that answers a NOTE 1 cause as a literal now sends invalidParams,
naming only what the handler itself knows. A recount on this branch finds 9 literal sites plus
the shared `expiry_problem` helper, all in `nfs/udm/src/main.cpp` (none in `stores.cpp`,
`map_server.cpp`, `map_client.cpp`, `li_poi.cpp`, `udr_auth_source.cpp` or the headers):

| Route (handler) | Cause | invalidParams[].param |
|---|---|---|
| `expiry_problem`, from SDM subscribe / modify (2 + 2 handlers) | `OPTIONAL_IE_INCORRECT` | `/expires` (SdmSubscription, SharedDataSubscription; reason: the `confirm_expiry_in` message) |
| `expiry_problem`, from EE subscribe / modify (2 handlers) | `OPTIONAL_IE_INCORRECT` | `/reportingOptions/expiry` (EeSubscription; reason as above) |
| SDM `GET /shared-data` | `MANDATORY_IE_MISSING` | `query shared-data-ids` |
| SDM `GET /group-data/group-identifiers` | `MANDATORY_IE_MISSING` | `query ext-group-id` and `query int-group-id` (reason: one of them is required) |
| UEAU `POST /{supiOrSuci}/security-information/generate-auth-data`, SUCI not de-concealed | `MANDATORY_IE_INCORRECT` | `{supiOrSuci}` (reason "could not be de-concealed") |
| UEAU `POST /{supiOrSuci}/prose-security-information/generate-av`, same | `MANDATORY_IE_INCORRECT` | `{supiOrSuci}` |
| UEAU `GET /{supiOrSuci}/security-information-rg`, no authenticated-ind | `MANDATORY_QUERY_PARAM_MISSING` | `query authenticated-ind` |
| same, SUCI not de-concealed | `MANDATORY_IE_INCORRECT` | `{supiOrSuci}` |
| UEAU `POST /{supi}/hss-security-information/eap-aka-prime/generate-av` without servingNetworkId | `MANDATORY_IE_MISSING` | `/servingNetworkId` (reason "required for hssAuthType eap-aka-prime") |
| UEID `POST /deconceal`, SUCI not de-concealed | `MANDATORY_IE_INCORRECT` | `/suci` (reason "could not be de-concealed") |

- `expiry_problem(e, at)` now takes the JSON Pointer it reports. Two helpers, `sdm_expires_at()`
  and `ee_expiry_at()`, hold the pointers each handler hands to `confirm_expiry_in`, and the same
  value is passed to `expiry_problem`, so the pointer reported is the one that was checked (the
  ADR-0609 shape).
- The three identical SUCI de-concealment 400s on `{supiOrSuci}` routes become one helper,
  `suci_not_deconcealed(raw)`, with the same title, detail and cause as before.
- Field and parameter names were checked against the REL-19 YAML: `TS29503_Nudm_SDM.yaml`
  (`shared-data-ids` `required: true` on GetSharedData, `ext-group-id` / `int-group-id`,
  `SdmSubscription.expires`), `TS29503_Nudm_EE.yaml` (`EeSubscription.reportingOptions`,
  `ReportingOptions.expiry`), `TS29503_Nudm_UEAU.yaml` (path variable `supiOrSuci`,
  `authenticated-ind` `required: true`, `HssAuthenticationInfoRequest.servingNetworkId`) and
  `TS29503_Nudm_UEID.yaml` (`DeconcealReqData.suci`).
- No status code, cause, title or detail changed.
- Test: `UdmInvalidParams.*` in `tests/integration/test_udm_invalid_params.cpp` (7 probes: one per
  kind of site above, the EE expiry excepted, see Disclosed).

**Out of scope, unchanged.** The UDM's `parse_json_body` sites (422 `MANDATORY_IE_MISSING` /
`MANDATORY_IE_INCORRECT` from a generated `from_json`) still send no invalidParams, as ADR-0607
disclosed: the nlohmann exception gives no JSON Pointer.

**Disclosed.**
- `GET /shared-data` answers `MANDATORY_IE_MISSING` for the missing `shared-data-ids` query
  parameter, which the YAML marks `required: true`. Table 5.2.7.2-1's `MANDATORY_QUERY_PARAM_MISSING`
  ("Query parameter which is defined as mandatory ... is not included in the URI of the request")
  is the better fit, but this step does not change causes. Left as is, for a decision of its own.
- `group-identifiers` likewise answers `MANDATORY_IE_MISSING` for two query parameters the YAML
  marks `required: false` each; the UDM's rule that one of them must be present was not checked
  against TS 29.503 stage 3 text in this ADR. Both are listed, with the same reason, as ADR-0609
  did for the UDR's identical check.
- On a modify (SDM merge patch, EE JSON Patch), the reported pointer is the member of the patched
  resource, not a pointer into the patch document; for the EE JSON Patch, `TS29571_CommonData.yaml`'s
  `reason` description asks a PATCH failure to name the failing operation's index, which the UDM
  does not know (the whole patch is applied before the expiry is confirmed).
- The `{supiOrSuci}` entries name the path variable; the reason does not say which de-concealment
  step failed (format, protection scheme or MAC), because `deconceal_suci_if_needed` returns only
  `std::nullopt`.
- The EE `/reportingOptions/expiry` path has no integration probe here (an EeSubscription needs a
  `monitoringConfigurations` map); it shares `expiry_problem` with the SDM probe that is tested.
- `MISSING_PARAMETER` (403): no UDM site answers it.

**Verified.** See the PR: clang-format-18 and a syntax-only compile of the changed translation
units in this worktree. Not built, and the integration tests were not run here: they need the lab
certificates, which this worktree does not read. The lead runs them in the main checkout.

**Rejected alternatives.**
- *Change the two `MANDATORY_IE_MISSING` query-parameter causes to
  `MANDATORY_QUERY_PARAM_MISSING` here.* A cause change is a behaviour change outside this step.
- *Hard-code the pointer inside `expiry_problem`.* The UDM confirms expiry at two different
  pointers (`/expires`, `/reportingOptions/expiry`); passing the pointer keeps the one checked and
  the one reported the same.
- *Report the SUCI de-concealment failures as `/supiOrSuci` (a body pointer).* On the UEAU routes
  the SUCI is the path variable, not a body member; only the UEID Deconceal carries it in the body
  (`/suci`).
- *Name only `query ext-group-id` at group-identifiers.* Either one would satisfy the check; naming
  one would suggest the other is not acceptable.
