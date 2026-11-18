## ADR-0590: NWDAF subscription 404s carry SUBSCRIPTION_NOT_FOUND; BSF and the other NWDAF 404s have no defined cause (SBI-03)

**Date:** 2026-11-19. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), NWDAF and BSF
parts. The NWDAF had 8 cause-less 404s and the BSF 6. TS 29.520 is in hand, and TS 29.521 and
TS 29.552 were fetched (MANIFEST.tsv).

**Decision:**

- NWDAF: two 404s carry `SUBSCRIPTION_NOT_FOUND` (TS 29.500 table 5.2.7.2-1). Their sibling
  operations on the same resources already used this cause:
  - the GET of an unknown VFL training or inference subscription (`register_vfl_crud`);
  - `subscriptions/{id}/unsubscribe-info` for an unknown ML model training subscription.
- `NwdafMlTrainingSub` asserts the cause on the second `unsubscribe-info`.
- BSF: no code change. TS 29.521 table 5.7.3-1 defines no 404 cause, and clause 4.2 only says
  "404 Not Found" for an unknown binding. So the 6 cause-less BSF 404s are already conformant.
- The other six NWDAF 404s also stay without a cause, because no spec in hand defines one:
  - analytics context transfers (two);
  - the ML model monitoring registration;
  - the FL callback correlation id;
  - the inbound DCCF fetch;
  - an analytics ID this NWDAF does not compute.

  TS 29.520's tables define no 404 cause, and `SUBSCRIPTION_NOT_FOUND` covers only
  subscriptions and their notifications.

Disclosed:

- The VFL subscription GET cause has no test, because there is no VFL integration test yet.
- The analytics-info 404 for an analytics ID this NWDAF does not compute is a reading of the
  YAML's 404. TS 29.520 clause 4.2 gives 204 No Content only when the requested analytics data
  does not exist, which is a different case. The status is unchanged here.
- Compile-checked locally only (this host is a CI runner); CI result to be added.

**Rejected alternatives.**
- `SUBSCRIPTION_NOT_FOUND` on the transfer, registration and FL-callback 404s: those resources are
  not subscriptions.
- A new BSF PR: it would change nothing.
