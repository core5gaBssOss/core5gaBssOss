## ADR-0533: NEF northbound PUT only replaces: 404 on a missing resource, no create-on-PUT (SPEC-07)

**Date:** 2026-10-26. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SPEC-07: NEF PUT handlers on northbound individual resources (TS 29.122,
TS 29.522) fell through to `create_with_id` when the addressed id did not exist and answered 201
without `Location`. An AF could create resources under ids it chose, bypassing the POST id
allocation, and a client expecting 404 for an unknown id got 201. At `24f30bf` 16 handlers did this
(the review counted 14 at `5ae0ef1`). For 14 of them the YAML's PUT responses are 200, 204, 307,
308 and errors -- no 201 -- and the stage-3 procedures describe the PUT as an update of an existing
resource: TS 29.122 4.4.5.3.1 (NIDD downlink data), 4.4.7.2.2.1 (TMGI expiration renewal),
4.4.7.2.2.4 and 4.4.7.2.3.4 (GMD via MB2 / xMB), 4.4.9 (CP provisioning), 4.4.10 (PFD management);
TS 29.522 4.4.24.1 and 4.4.24.2 (time sync), 4.4.28.3 (EAS deployment), 4.4.29.4.2 (MBS parameters
provisioning), 4.4.40.3 (ECS address), 4.4.43.2.3 (UAV flight assistance).

**Decision.** On these 14 paths PUT replaces an existing resource only (`nfs/nef/src/main.cpp`):
an existing resource is replaced and answered 200 with its representation (unchanged; the YAML
also allows 204); a missing one is answered 404 ProblemDetails and nothing is created.

| Path (root + resource) | YAML (`specs/5G_APIs-REL-19/`) | 404 cause |
|---|---|---|
| `/3gpp-cp-parameter-provisioning/v1/{scsAsId}/subscriptions/{subscriptionId}` | TS29122_CpProvisioning.yaml:141 | SUBSCRIPTION_NOT_FOUND |
| `/3gpp-cp-parameter-provisioning/v1/{scsAsId}/subscriptions/{subscriptionId}/cpSets/{setId}` | TS29122_CpProvisioning.yaml:307 | none |
| `/3gpp-group-message-delivery-mb2/v1/{scsAsId}/tmgi-allocation/{tmgi}` | TS29122_GMDviaMBMSbyMB2.yaml:125 | none |
| `/3gpp-group-message-delivery-mb2/v1/{scsAsId}/tmgi-allocation/{tmgi}/delivery-via-mbms/{transactionId}` | TS29122_GMDviaMBMSbyMB2.yaml:501 | none |
| `/3gpp-group-message-delivery-xmb/v1/{scsAsId}/services/{serviceId}/delivery-via-mbms/{transactionId}` | TS29122_GMDviaMBMSbyxMB.yaml:382 | none |
| `/3gpp-nidd/v1/{scsAsId}/configurations/{configurationId}/downlink-data-deliveries/{downlinkDataDeliveryId}` | TS29122_NIDD.yaml:413 | none |
| `/3gpp-pfd-management/v1/{scsAsId}/transactions/{transactionId}` | TS29122_PfdManagement.yaml:191 | none |
| `/3gpp-pfd-management/v1/{scsAsId}/transactions/{transactionId}/applications/{appId}` | TS29122_PfdManagement.yaml:389 | none |
| `/3gpp-eas-deployment/v1/{afId}/eas-deployment-info/{easDeployInfoId}` | TS29522_EASDeployment.yaml:129 | none |
| `/3gpp-ecs-address/v1/{afId}/configurations/{configId}` | TS29522_ECSAddress.yaml:128 | none |
| `/3gpp-mbs-session/v1/mbs-pp/{mbsPpId}` | TS29522_MBSSession.yaml:476 | none |
| `/3gpp-time-sync/v1/{afId}/subscriptions/{subscriptionId}` | TS29522_TimeSyncExposure.yaml:169 | SUBSCRIPTION_NOT_FOUND |
| `/3gpp-time-sync/v1/{afId}/subscriptions/{subscriptionId}/configurations/{instanceReference}` | TS29522_TimeSyncExposure.yaml:484 | none |
| `/3gpp-uav-fa/v1/{afId}/configurations/{configurationId}` | TS29522_UAVFlightAssistance.yaml:179 | none |

404 causes:
- SUBSCRIPTION_NOT_FOUND (TS 29.500 Table 5.2.7.2-1: "The request for modification or deletion of a
  subscription [...] is rejected because the subscription is not found in the NF") for the two
  resources the specs name subscriptions ("Individual CP Provisioning Subscription", "Individual
  Time Synchronization Exposure Subscription"). It applies through TS 29.122 5.2.6 ("The protocol
  and application errors in clause 5.2.7.2 of 3GPP TS 29.500 [44] are applicable"), and for
  TimeSyncExposure through TS 29.522 5.15.6.1, which points to TS 29.122 5.2.6.
- No cause (`std::nullopt`, the `problem_response` contract for "neither defines one") for the other
  12. TS 29.500 Table 5.2.7.2-1 has no not-found cause for a resource that is not a subscription;
  the API-specific tables have none that fits a never-created id: TS 29.122 5.6.5.3
  ALREADY_DELIVERED is for buffered data already delivered; TS 29.522 5.20.7.3
  MBS_SESSION_CONTEXT_NOT_FOUND is for an Individual MBS Session, not the MBS Parameters
  Provisioning resource; TS 29.122 5.8 (GMD) and 5.11 (PFD) and TS 29.522 5.15.6.3, 5.21.6.3,
  5.36.7.3 and 5.39.7.3 list no 404 error. TS 29.122 Table 5.2.6-1's 404 ("The resource URI was
  incorrect") carries none.

Specs read: TS 29.122 j70, TS 29.522 j70, TS 29.500 j71 (`tools/specs/fetch_3gpp_specs.py`;
`specs/3gpp/MANIFEST.tsv` still records 29.500 j70, left unchanged).

Amends ADR-0327 ("Several of these APIs define PUT as an upsert"): true only for the two paths
below; a note is added there.

**Rejected alternatives.**
- *Keep create-on-PUT and add `Location` to the 201.* No 201 in the YAML on these paths; creation is
  POST on the collection, where the NEF allocates the id.
- *RESOURCE_URI_STRUCTURE_NOT_FOUND for a missing id.* TS 29.500 defines it for a path segment that
  matches neither a variable format nor a fixed part, not for a well-formed id with no resource.
- *ALREADY_DELIVERED, MBS_SESSION_CONTEXT_NOT_FOUND or UE_ID_NOT_AVAILABLE.* Defined for other
  conditions (above).
- *204 on update.* Allowed by the YAML; 200 with the representation is kept (existing behaviour;
  the YAML's 200 descriptions ask for the updated resource).

**Disclosed.**
- Two PUT handlers still create on PUT because their YAML defines 201: NIDD
  `rds-ports/{portId}` (TS29122_NIDD.yaml:671, 201/202 only; a ManagePort configuration is created
  by PUT, TS 29.122 4.4.5.6.1) and AM PolicyAuthorization `events-subscription`
  (TS29522_AMPolicyAuthorization.yaml:279, 201/200/204). Their own deviations, the review's SPEC-07
  "Related" items, stay open: rds-ports answers 200 on an existing port, which its YAML does not
  list; the events-subscription 201 has no `Location`, which its YAML marks `required: true`.
- GET and DELETE on the two subscription resources still answer 404 without a cause (SBI-03's open
  404 causes); only PUT was in scope.
- TS 29.122 4.4.9 lets a CP Provisioning subscription PUT add CP parameter sets as new
  `cpSets/{setId}` sub-resources; the handler stores the body and creates no sub-resources
  (unchanged).
- The 404 bodies' `detail` texts are the existing ones of each resource's GET/DELETE handlers.

**Verified (2026-10-26).**
- YAML PUT response sets read per path with a YAML parser (table above); a script compared every
  status code each of the 14 handlers can return with its path's PUT responses: all within.
- `g++` 13.3 and `clang++` 18.1.3, `-std=c++23 -fsyntax-only` with the project warning flags, on
  `nfs/nef/src/main.cpp` and `tests/integration/test_nef_put_replaces_only.cpp`, against headers
  generated by `tools/sbi-codegen` from the full pilot list: no errors; no new `main.cpp` warnings
  compared with `24f30bf`. `clang-format-18 --dry-run -Werror` clean.
  `tests/conformance/validate_api_roots.py`: PASS.
- Tests added, not run here (no build tree in this environment): `NefPutReplacesOnly.*` -- PUT on a
  missing and on an existing PFD management transaction (404 without cause, nothing created; 200
  with the replacement) and time sync subscription (404 SUBSCRIPTION_NOT_FOUND; 200).
