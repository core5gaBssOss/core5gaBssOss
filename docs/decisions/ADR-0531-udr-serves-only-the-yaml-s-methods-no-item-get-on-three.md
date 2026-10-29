## ADR-0531: UDR serves only the YAML's methods: no item GET on three application-data resources, no pseudo-operation (SPEC-08/09)

**Date:** 2026-10-24. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SPEC-08: the UDR served GET on `bdtPolicyData/{bdtPolicyId}`,
`iptvConfigData/{configurationId}` and `serviceParamData/{serviceParamId}`, which
TS29519_Application_Data.yaml defines with PUT, PATCH and DELETE only (reads are the collection
GETs with filters). SPEC-09: it served `POST /data-restoration-events`, which TS29504_Nudr_DR.yaml
marks "a pseudo operation, clients shall NOT invoke this method!" (it only describes the
restorationNotification callback). User direction 2026-10-24: no deviation from 3GPP.

**Decision.** The three item GETs and the pseudo-operation route are removed; the paths now carry
exactly the YAML's methods (verified against the YAML: put, patch, delete). No caller in the tree
used them (the NEF uses PUT/PATCH/DELETE only). The data-restoration subscription store stays, unused,
for a future restorationNotification sender. SPEC-10 (UDSF `INSUFFICIENT_SCOPES`) was already removed
by `d9ef9dc` (ADR-0499 phase 4: the UDSF uses the shared authorizer).

**Rejected alternatives.** *Keeping them as a disclosed project extension* -- the user ruled out
deviations.

**Disclosed.** ADR-0254's "GET/PUT/PATCH/DELETE item" coverage line is superseded for these three
resources.

**Verified (2026-10-24).** See below.
