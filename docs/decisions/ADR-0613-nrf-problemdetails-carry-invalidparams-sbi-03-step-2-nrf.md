## ADR-0613: NRF ProblemDetails carry invalidParams (SBI-03 step 2, NRF)

**Date:** 2026-11-22. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that takes an
`InvalidParam` list and four builders for the `param` encodings of `TS29571_CommonData.yaml`
(`InvalidParam.param`: a JSON Pointer for a body attribute, `"query "` plus the name for a query
parameter, `"header "` plus the name for a header, `{name}` for a path variable). TS 29.500
V19.7.0 clause 5.2.7.2, Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in
the 'ProblemDetails' data structure indicating unsupported, missing or incorrect IE(s) or query
parameter(s) or 3gpp-Sbi-Discovery-* header(s)." The NOTE 1 causes are `INVALID_QUERY_PARAM`,
`MANDATORY_QUERY_PARAM_INCORRECT`, `OPTIONAL_QUERY_PARAM_INCORRECT`,
`MANDATORY_QUERY_PARAM_MISSING`, `MANDATORY_IE_INCORRECT`, `OPTIONAL_IE_INCORRECT`,
`MANDATORY_IE_MISSING` (400) and `MISSING_PARAMETER` (403). This ADR is step 2 for one NF, the
NRF (`nfs/nrf`), in the shape of the UDR's ADR-0609.

**Decision.** Every NRF site that answers a NOTE 1 cause now sends invalidParams, naming only what
the handler itself checked. A grep of `nfs/nrf` on this branch finds 5 such sites, all in
`nfs/nrf/src/main.cpp` (none in `nf_update.cpp`, `registry.cpp`, `discovery_authz.cpp`,
`token_authz.cpp` or the headers):

| Route (operation) | Cause | invalidParams[].param |
|---|---|---|
| `POST /oauth2/retrieve-key` (RetrieveKeyRequest) | `MANDATORY_IE_MISSING` | each absent one of `/issuer`, `/headerParameters` (both `required` in `RetrieveKeyReq`, `TS29510_Nnrf_AccessToken.yaml`) |
| `PUT /nnrf-nfm/v1/nf-instances/{nfInstanceID}` (NFRegister / NFUpdate by PUT) | `MANDATORY_IE_MISSING` | each absent one of `/nfInstanceId`, `/nfType`, `/nfStatus` (`NFProfile.required`, `TS29510_Nnrf_NFManagement.yaml`) |
| `GET /nnrf-disc/v1/nf-instances` (NFDiscover), no target-nf-type | `MANDATORY_QUERY_PARAM_MISSING` | `query target-nf-type` |
| same, `requester-plmn-list` or `requester-snssais` not a JSON array | `MANDATORY_IE_INCORRECT` | `query requester-plmn-list` or `query requester-snssais` (reason "not a JSON array") |
| `POST /nnrf-nfm/v1/subscriptions` (NFStatusSubscribe) | `MANDATORY_IE_MISSING` | `/nfStatusNotificationUri` (`SubscriptionData.required`) |

- One helper in `main.cpp`'s anonymous namespace, `missing_body_members(body, names)`, lists only
  the required top-level members the request body lacks, as JSON Pointers built with
  `json::json_pointer` (so RFC 6901 escaping is the library's). The RetrieveKeyReq and NFProfile
  checks used to say "requires issuer and headerParameters" / name only the first absent member
  without saying in a machine-readable form which ones were absent.
- No status code, cause, title or detail changed. No counter changed.
- Test: `NrfInvalidParams.*` in `tests/integration/test_nrf_invalid_params.cpp` (real nrf process,
  TLS 1.3 + mTLS HTTP/2, a real NRF token where the route needs one), one probe per kind of site:
  `DiscoveryWithoutTargetNfTypeNamesTheQueryParameter`,
  `NonArrayRequesterPlmnListNamesTheQueryParameter`,
  `RegisterListsOnlyTheAbsentRequiredNfProfileMembers`,
  `SubscribeWithoutNotificationUriNamesTheBodyAttribute`,
  `RetrieveKeyWithoutHeaderParametersNamesOnlyThatMember`.

**Out of scope, unchanged.**
- The NRF's other 400s are `INVALID_MSG_FORMAT` (malformed JSON, a failed JSON Patch) or
  `UNSPECIFIED_MSG_FAILURE` (`validate_nf_profile`), which NOTE 1 does not cover.
- `POST /oauth2/token` errors are `AccessTokenErr` (RFC 6749 `error`), not ProblemDetails, so
  invalidParams does not apply to them.
- The NRF reads no `3gpp-Sbi-Discovery-*` header, so no `header ...` entry arises here.

**Disclosed.**
- NFRegister: the `detail` still names only the first absent NFProfile member (unchanged), while
  invalidParams names every absent one of the three. They agree on the first entry.
- NFDiscover: the `MANDATORY_QUERY_PARAM_MISSING` 400 is also answered when `target-nf-type` is
  present with an empty value. `query target-nf-type` is named either way; the reason does not
  say which of the two it was.
- NFDiscover: `requester-plmn-list` and `requester-snssais` are optional query parameters
  (`TS29510_Nnrf_NFDiscovery.yaml`, no `required: true`), yet the site answers
  `MANDATORY_IE_INCORRECT`. `OPTIONAL_QUERY_PARAM_INCORRECT` looks the better fit, but this step
  does not change causes. Left as is, for a decision of its own.
- `missing_body_members` checks top-level members only. Every current caller checks the root
  object's required members, so the pointers are exact; a nested check would need a different
  pointer.
- RetrieveKeyRequest: an `issuer` that is present but not a string makes
  `body.at("issuer").get<std::string>()` throw after the check; that path sends no
  invalidParams. It is not one of the NOTE 1 sites (it answers no NOTE 1 cause literal) and was
  not changed.
- No NRF site answers `MISSING_PARAMETER` (403), `INVALID_QUERY_PARAM`,
  `MANDATORY_QUERY_PARAM_INCORRECT`, `OPTIONAL_QUERY_PARAM_INCORRECT` or `OPTIONAL_IE_INCORRECT`
  today; SearchNFInstances still filters on target-nf-type only and ignores other query
  parameters rather than refusing them (the file's own disclosed simplification).

**Verified.** clang-format-18 `--dry-run -Werror` on the changed C++ files, and a syntax-only
compile (`-fsyntax-only`, flags from the main checkout's `compile_commands.json`) of
`nfs/nrf/src/main.cpp` and `tests/integration/test_nrf_invalid_params.cpp`. Not built, not run:
the integration tests need the lab certificates, which this worktree does not read; the lead runs
them in the main checkout.

**Rejected alternatives.**
- *Name every required member at the multi-member sites, present or not.* NOTE 1 asks for the
  missing ones; listing a present one would be wrong.
- *Name only the first absent NFProfile member, matching the detail.* The handler can see every
  absent one at no cost, and NOTE 1 says "IE(s)"; reporting one of two missing would make the
  consumer fix them one round trip at a time.
- *Change the requester-plmn-list / requester-snssais cause to `OPTIONAL_QUERY_PARAM_INCORRECT`
  here.* A cause change is a behaviour change outside this step.
- *A shared sbi-core helper for absent body members.* The UDR (ADR-0609) has a query-parameter
  helper of its own and the NRF needs only a body one; lifting either to sbi-core belongs to a
  step that sees all NFs' call sites, not to one NF's PR.
