## ADR-0577: NRF error responses: retrieve-key 404 carries UNKNOWN_ISSUER; the other NRF 404s have no defined cause (SBI-03, NRF part)

**Date:** 2026-11-15. **Status:** Accepted (user, 2026-11-18). Proposed (user, 2026-11-15: "Start with the NRF for SBI-03").
Item 4 of the v0.2.0 plan (ADR-0572), first NF. Continues ADR-0502 (SBI-03, `cause` required on
every `problem_response`). Numbered 0577 because ADR-0576 is taken by PR #23.

**Context.** ADR-0572 item 4 reads "the remaining 546 error responses get their table 5.2.7.2-1
`cause`". Before touching the NRF the open sites were counted again (`problem_response(<status>, ...,
std::nullopt)` in `nfs/` and `libs/`, generated code excluded): 519, of which **482 are 404**, 26
are 501, 3 are 504, 3 are 503, 2 are 403 and 1 each 502, 409 and 415. The NRF has 5, all 404:

| Site (`nfs/nrf/src/main.cpp`) | Operation | Cause defined by the spec? |
|---|---|---|
| `POST /oauth2/retrieve-key`, unknown issuer | Nnrf_AccessToken RetrieveKey | **Yes**: `UNKNOWN_ISSUER`, 404 (TS 29.510 tables 6.3.4.3.2-2 and 6.3.6.3-1) |
| NFUpdate/NFDeregister/NFProfile of an unknown `nfInstanceID` (2 sites) | Nnrf_NFManagement | No |
| Stored search / complete search of an unknown `searchId` (2 sites) | Nnrf_NFDiscovery | No |

TS 29.500 table 5.2.7.2-1 has only two 404 causes, `SUBSCRIPTION_NOT_FOUND` (a subscription,
already used by the NRF) and `RESOURCE_URI_STRUCTURE_NOT_FOUND` (an `apiSpecificResourceUriPart`
that matches no known format, which a missing instance id is not). TS 29.510 (j80) Tables 6.1.7.3-1,
6.2.7.3-1 and 6.3.6.3-1 define no other 404 application error. `NF_NOT_FOUND` appears in that text
only in the change history (CR 0974, CT#103), not in a table or the YAML (not found in
`TS29510_*.yaml`), so it is not used.

**Decision:**

- `POST /oauth2/retrieve-key` for an unknown issuer answers 404 with `cause` `UNKNOWN_ISSUER`.
- The four other NRF 404s keep `std::nullopt`: the spec in hand defines no cause for them, and
  inventing one is what CLAUDE.md guardrail 1 forbids.
- **The scope of ADR-0572 item 4 is restated:** the ~519 open sites are 93% 404s whose cause, where
  one exists, is an application error in each API's stage-3 text (ADR-0502 already said so). Per NF,
  the work is to read that NF's TS 29.5xx application-error tables and assign a cause only where a
  table names one; the rest stay `std::nullopt` and are listed in the NF's ADR. It is not 546
  assignments from Table 5.2.7.2-1.

**Rejected alternatives.**

- **`RESOURCE_URI_STRUCTURE_NOT_FOUND` for an unknown NF instance.** Its definition is about a path
  segment that matches no expected format; a well-formed UUID of an unregistered instance is not that.
- **`NF_NOT_FOUND`.** Not in any table or the YAML of the specification in hand.
- **A project-defined cause such as `NF_INSTANCE_NOT_FOUND`.** `cause` is an open string in
  ProblemDetails, but a name no spec defines is an invented wire value.

**Disclosed.**

- Four NRF 404s carry no `cause`: not conformant to a reading in which every ProblemDetails has one;
  the spec in hand gives none.
- The 400s on `retrieve-key` use `MANDATORY_IE_MISSING`/`INVALID_MSG_FORMAT`; TS 29.510 says the 400
  cause "should be" `KEY_IDENTIFICATION_FAIL`, which is for a key that cannot be identified from
  well-formed input. Not changed here.
- The NRF's other error paths are outside `main.cpp`'s `problem_response` calls (the sbi-core
  Authorizer's 401/403, the generic 404/405 for an unknown route) and belong to the platform
  libraries.
- The 26 501s, 3 504s and 3 503s elsewhere were counted, not examined.

**Verified (2026-11-15, this machine).** `NrfGapClosureIntegration.RetrieveKeyRequestForUnknownIssuerIs404`,
now asserting `cause == "UNKNOWN_ISSUER"`, and `RetrieveKeyRequestForOwnIssuerReturnsRealPublicKey`
both pass against the rebuilt NRF. The second failed once with `bind: Address already in use` when
run straight after the first (port 7777 not yet released) and passed on its own. The assertion was
not run against the unfixed NRF, so this records that the test passes with the fix, not that it
fails without it. CI on PR #24 passed: build, lint, asan-ubsan and tsan.
