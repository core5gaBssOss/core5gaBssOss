## ADR-0583: UDR 404s carry DATA_NOT_FOUND, SUBSCRIPTION_NOT_FOUND or GROUP_IDENTIFIER_NOT_FOUND (SBI-03, UDR part)

**Date:** 2026-11-18. **Status:** Proposed (user, 2026-11-15: continue SBI-03 one NF per PR, per
ADR-0577's restated scope; 2026-11-18: "move to next phases"). Item 4 of the v0.2.0 plan
(ADR-0572), third NF, after the NRF (ADR-0577) and the NEF (ADR-0578).

**Context.** ADR-0578 left the UDR's SBI-03 work blocked, because TS 29.505, 29.519 and 29.506 were
not in `specs/3gpp/`. They were fetched on 2026-11-18 (TS 29.505 j60, 29.519 j80, 29.506 j30;
`tools/specs/fetch_3gpp_specs.py`).

What the texts say:
- TS 29.519 (tables 5.5.3-1, 6.5.3-1 and 7.4.3-1) defines no application errors of its own. The same
  holds for TS 29.506 clause 6.1.6. Each defers to TS 29.504 clause 6.1.6.
- TS 29.504 table 6.1.6-2 says, in its NOTE, that its codes apply to TS 29.505, 29.519 and 29.506. It
  defines three 404 causes:
  - `USER_NOT_FOUND`: "the user indicated in the HTTP/2 request does not exist in the UDR";
  - `DATA_NOT_FOUND`: "the data indicated in the HTTP/2 request is unavailable in the UDR";
  - `GROUP_IDENTIFIER_NOT_FOUND`: "the group identifier does not exist".

  `PLMN_NOT_FOUND` is a fourth 404 cause, but it is not used here.
- TS 29.505 lists `GROUP_IDENTIFIER_NOT_FOUND` for GroupIdentifiers GET (5.2.33.3.1), Individual5GVnGroup
  PATCH (5.2.35.3.3) and Individual5GmbsGroup PATCH (5.2.55.3.3). It lists `SUBSCRIPTION_NOT_FOUND`,
  which comes from TS 29.500 table 5.2.7.2-1, for the individual subscription resources.

ADR-0578 said that TS 29.504 "defines only `USER_NOT_FOUND` for Nudr_GroupIDmap". That was a misreading:
table 6.1.6-2 covers the whole `nudr-dr` API.

At `main` 3d02404f, `nfs/udr/src/main.cpp` had 165 `problem_response(404, ...)` calls. Two already
carried `SUBSCRIPTION_NOT_FOUND`, and the provisioned guard (ADR-0565) answers `USER_NOT_FOUND`. The
other 163 carried no cause.

**Decision:** each of the 163 gets a cause. The class is chosen by the resource:
- **`SUBSCRIPTION_NOT_FOUND` (25 sites)** for an individual subscription resource. These are the EE,
  SDM and EE-group subscriptions, the policy-data and application-data subs-to-notify, the
  exposure-data subscriptions, and the individual `subscriptions/{subscriptionId}` items.
- **`GROUP_IDENTIFIER_NOT_FOUND` (7 sites)**:
  - GroupIdentifiers GET;
  - Individual5GVnGroup GET, PATCH and DELETE;
  - Individual5GmbsGroup GET, PATCH and DELETE.

  That is, where the missing thing is the group identifier itself.
- **`DATA_NOT_FOUND` (131 sites)** for every other 404. In these the data named by the request is
  absent: subscription data, context data, policy data, application data and exposure data.

  This also covers the "subscription info" sub-resources (AMF, SMF and HSS subscription info under an
  EE or SDM subscription). Those are data attached to a subscription, not the subscription resource
  itself.

  It also covers the query routes over internal group ids ("No 5G VN Group matches the requested
  internal-group-ids"), because there the request names no single group identifier.

The cause is a string literal at each site, as in ADR-0577 and ADR-0578. No consumer NF branches on
these causes (grep of `nfs/`, `bss/` and `libs/`), so no caller changes behaviour.

Test `UdrNotFoundCauses.*` (`tests/integration/test_udr_404_causes.cpp`) uses a real NRF and UDR over
mTLS. It has one probe per class:
- `GET .../context-data/amf-3gpp-access` gives `DATA_NOT_FOUND`;
- `GET application-data/subs-to-notify/{unknown}` gives `SUBSCRIPTION_NOT_FOUND`;
- `GET group-data/5g-vn-groups/{unknown}` gives `GROUP_IDENTIFIER_NOT_FOUND`.

**Rejected alternatives.**
- **`USER_NOT_FOUND` on reads under `subscription-data/{ueId}` for a UE that was never provisioned.**
  This would be more precise, but the read routes do not consult `udr_provisioned_data`. ADR-0565
  guards writes only. Adding the lookup to every read is a behaviour change with its own cost, so it
  is deferred for future implementation. Until then such a read answers `DATA_NOT_FOUND`, which
  table 6.1.6-2 also allows, since the data is unavailable.
- **Leaving the 404s without a cause, as TS 29.519's NOTE permits ("optional").** The TS 29.500
  table 5.2.7.2-1 reading of ADR-0502 is that the cause is supplied wherever one is defined. The
  user chose to continue SBI-03.
- **A helper such as `not_found_data(detail)`.** It was rejected so that each site stays greppable
  for its literal cause, as in ADR-0577 and ADR-0578. A follow-up can add one if the remaining NFs
  make it worthwhile.

**Disclosed.**
- The class of each site was chosen from its detail text and route, by a script followed by
  review. Only one site per class is exercised end to end.
- `clang-format-18` re-wrapped the changed calls, so the diff is larger than the 163 changed literals.
- The UDR's non-404 errors were not examined in this change.
- The UDR's `oam_provisioning.cpp` has no 404s.
- The other NFs' 404s are still open. The remaining count is in REVIEW-5ae0ef1 SBI-03.
- Verified locally only with `-fsyntax-only` on the two changed translation units, because the
  self-hosted runners were busy and this host runs CI (`ci-runner-shares-host`). The CI result is to
  be added.
