## ADR-0595: DCCF subscription DELETE 404 carries SUBSCRIPTION_NOT_FOUND (SBI-03)

**Date:** 2026-11-20. **Status:** Proposed.

**Context.** SBI-03 (`docs/reviews/REVIEW-5ae0ef1.md`), v0.2.0 item 4 (ADR-0572), DCCF part. The
DCCF had 3 cause-less 404s. TS 29.574 (j70) is in `specs/3gpp/MANIFEST.tsv`. Its application error
tables define no 404 cause: table 5.1.7.3-1 (Ndccf_DataManagement) has only
`SUBSCRIPTION_CANNOT_BE_SERVED` (400), and table 5.2.7.3-1 (Ndccf_ContextManagement) is empty.

**Decision:**

- DELETE of an unknown Individual DCCF Data or Analytics Subscription answers 404 with
  `SUBSCRIPTION_NOT_FOUND`, the common cause of TS 29.500 table 5.2.7.2-1 ("the request for
  modification or deletion of a subscription ... is rejected because the subscription is not
  found"). The PUT of an unknown subscription already sent it.
- Test: `test_dccf.cpp` asserts the cause on the second DELETE of the same subscription.

Unchanged: PUT and DELETE of an unknown `data-collection-profiles/{profileId}`
(Ndccf_ContextManagement). A profile is not a subscription, no cause is defined for it, and
TS 29.500 clause 5.2.7.2 answers an unknown resource with a plain 404.

Disclosed: compile-checked locally only (this host is a CI runner); the PR's sanitizer runs were cancelled after merge, before they finished (build and lint passed). On `main`: asan-ubsan passed at fdbeb4af and tsan at cc995945 (2026-11-20, runs 44763962285 and 44791974533), both containing this change.

**Rejected alternatives.**
- Leaving the DELETE cause-less while the PUT on the same resource sends `SUBSCRIPTION_NOT_FOUND`.
