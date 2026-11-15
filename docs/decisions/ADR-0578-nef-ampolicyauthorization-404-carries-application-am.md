## ADR-0578: NEF AMPolicyAuthorization 404 carries APPLICATION_AM_CONTEXT_NOT_FOUND; the other NEF 404s have no defined cause (SBI-03, NEF part)

**Date:** 2026-11-15. **Status:** Proposed (user, 2026-11-15: continue SBI-03 per ADR-0577's restated
scope; the NEF was chosen over the UDR, below). Item 4 of the v0.2.0 plan (ADR-0572), second NF.
Continues ADR-0502 and ADR-0577. Numbered 0578 because ADR-0576 (#23) and ADR-0577 (#24) are open.

**Context.** The NEF has 193 `problem_response` calls with no cause: 174 404s (mostly "No such <resource>",
two "no IP-to-identity source") and 19 others (16 501 "no downstream is wired", one each 502, 503,
504). The NEF's northbound
APIs are TS 29.122 and TS 29.522. Their "Application Errors" clauses (29.122 5.x.5.3, 29.522 5.x.y.3,
all read in the copies under `specs/3gpp/`) list almost no 404 causes: the tables of most APIs
are empty ("Application Error / HTTP status code / Description" with no rows) and a missing
Individual resource has no cause in TS 29.500 table 5.2.7.2-1 either (only `SUBSCRIPTION_NOT_FOUND`,
already used where a subscription is meant, and `RESOURCE_URI_STRUCTURE_NOT_FOUND`).

404 application errors that do exist in the NEF's specs: `APPLICATION_AM_CONTEXT_NOT_FOUND`
(TS 29.522 table 5.17.5.3-1, "the specified Individual Application AM Context does not exist"),
`UE_ID_NOT_AVAILABLE` and `UE_NOT_FOUND` (UEId, CpProvisioning and NpConfiguration, 29.122 5.10.5.3,
5.13.5.3 and 29.522 5.25.7.3) and, in prose, `ALREADY_DELIVERED` and `PORT_NOT_ASSOC_WITH_APP` (NIDD).

The UDR was not chosen: its 166 open sites are the Nudr_DataRepository resources, whose application
errors are in TS 29.505 (subscription data), TS 29.519 (policy and application data) and TS 29.506
(AIoT), none of which is in `specs/3gpp/`; TS 29.504 itself defines only `USER_NOT_FOUND` for
Nudr_GroupIDmap. CLAUDE.md: when the spec text is not in hand, stop and ask. See Disclosed.

**Decision:**

- `GET`, `PATCH` and `DELETE` of `/3gpp-am-policyauthorization/v1/{afId}/app-am-contexts/{appAmContextId}`
  for an unknown context answer 404 with `cause` `APPLICATION_AM_CONTEXT_NOT_FOUND` (3 sites).
- The 171 other 404s and the 19 non-404 sites keep `std::nullopt` where nothing in the specification
  in hand defines a cause. In particular the fourth AM-policy site (`DELETE .../events-subscription`)
  keeps none: its missing object is the event subscription of a context, not the context named by
  the application error.

**Rejected alternatives.**

- **`SUBSCRIPTION_NOT_FOUND` for the many "No such ... subscription" 404s.** Its definition is a
  subscription "in the NF" for modification, deletion or notification; some of these routes are
  subscriptions and could take it, but doing it by wording of the error text (as ADR-0502 did for
  the 121 sites it assigned) is the guess this review is meant to remove. Each route needs checking
  against its API's text; not done here.
- **Map `UE_NOT_FOUND`/`UE_ID_NOT_AVAILABLE` now.** They are defined for a UE-identity lookup that the
  NEF answers from other state (the 404 "no IP-to-identity source" sites); mapping them needs the
  procedure text for each of the three APIs read against the code. Not done here.

**Disclosed.**

- Partial: 3 of the NEF's 193 cause-less responses now carry a cause. The rest were classified by the
  application-error tables only, not site by site. Candidates still open: the `UE_ID_NOT_AVAILABLE`/
  `UE_NOT_FOUND` sites, the NIDD `ALREADY_DELIVERED` and `PORT_NOT_ASSOC_WITH_APP` cases, and the
  subscription 404s that may take `SUBSCRIPTION_NOT_FOUND`.
- The UDR's SBI-03 work is blocked on TS 29.505, 29.519 and 29.506 text. Please supply the texts
  or say to leave the UDR's 404s as they are.
- The `.gitignore` gains the architecture picture that was sitting untracked in the repository root
  (user, 2026-11-15: add to `.gitignore`).

**Verified (2026-11-15, this machine).** `NefAmContextCause.UnknownContextIs404WithApplicationAmContextNotFound`
(GET, DELETE and PATCH of an unknown context) passes against the rebuilt NEF. It was not run against
the unfixed NEF, so this records that it passes with the fix, not that it fails without it. CI result
to be added.
