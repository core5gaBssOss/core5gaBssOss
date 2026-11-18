## ADR-0584: UDM 404s carry the TS 29.503 cause for their service (SBI-03, UDM part)

**Date:** 2026-11-18. **Status:** Proposed. On 2026-11-18 the user chose "Both" for the next items:
continue SBI-03 with the next NF, and draft the TMF640 DELETE procedure list. This is item 4 of the
v0.2.0 plan (ADR-0572), fourth NF, after the NRF (ADR-0577), the NEF (ADR-0578) and the UDR
(ADR-0583, PR #32). Numbered 0584 because ADR-0583 is open in #32.

**Context.** At `main` 3d02404f, `nfs/udm/src/main.cpp` had 36 `problem_response(404, ...)` calls
with no `cause`. TS 29.503 j70 has an application-error table for each UDM service (tables 6.1.7.3-1
to 6.8.7.3-1). Its resource clauses also list, for each method, the 404 causes that the "cause"
attribute may carry.

**Decision:** each site gets the cause that TS 29.503 lists for its resource and method:

| Sites | Cause | Source |
|---|---|---|
| 24 UECM registration absences: AMF 3GPP and non-3GPP access, SMSF 3GPP and non-3GPP access, IP-SM-GW, NWDAF and SMF registrations (GET/PATCH/DELETE and the AMF custom operations); `send-routing-info-sm` with no SMSF or IP-SM-GW registration; `restore-pcscf`, `location` and `auth-trigger` with no AMF registration | `CONTEXT_NOT_FOUND` | table 6.2.7.3-1; 6.2.3.2.3.2-3, 6.2.3.5.2.3-4, 6.2.3.6.3.3-4, 6.2.3.8.3.1 and others |
| 4 SDM `provisioned-data` / `lcs-bca-data` / `nssai` reads, SDM `shared-data/{sharedDataId}`, UEAU `auth-events/{authEventId}` PUT | `DATA_NOT_FOUND` | table 6.1.7.3-1; 6.3.3.6.2.1 |
| SDM `group-data/group-identifiers` | `GROUP_IDENTIFIER_NOT_FOUND` | table 6.1.7.3-1 |
| UEAU generate-auth-data with no authentication subscription; SDM `id-translation-result` with no identity or SUPI; PP `pp-data` GET | `USER_NOT_FOUND` | 6.3.3.2.4.2.2; table 6.1.7.3-1; 6.5.3.2.3.2 |
| SSAU remove for an unknown authorization | `AUTHORIZATION_NOT_FOUND` | 6.8.3.2.4.3.2 |

The cause is a literal at each site, as in ADR-0577, ADR-0578 and ADR-0583. No consumer NF branches on
these causes (grep of `nfs/`, `bss/` and `libs/`).

Test: `UdmSmsfGapClosureIntegration.Smsf3GppAndNon3GppRegistrationGroupsFullLifecycle` (`tests/integration/test_udm_uecm_gap_closure_217.cpp`). It now asserts
`CONTEXT_NOT_FOUND` on the SMSF registration GET-before-PUT and on DELETE-again. It runs for both
accesses.

**Rejected alternatives.**
- **`ABSENT_SUBSCRIBER_SM` for `send-routing-info-sm` with no registration.** TS 29.503 describes it
  as "SMSF registered but not reachable". Here nothing is registered, so `CONTEXT_NOT_FOUND`
  (table 6.2.7.3-1, "no corresponding context exists") was chosen instead. 6.2.3.9.4.2.2 lists only
  `USER_NOT_FOUND` and `ABSENT_SUBSCRIBER_SM` for that operation, so this choice is a reading, not the
  letter.
- **`USER_NOT_FOUND` for every registration absence.** The UDM keeps registrations in its own store
  and does not check whether the UE exists in the UDR before it answers. So it cannot claim that the
  user does not exist.

**Disclosed.**
- The two AMF custom operations (`dereg-amf`, `pei-update`; 6.2.3.2.4.x) list only `USER_NOT_FOUND`
  for 404. They now answer `CONTEXT_NOT_FOUND` from the service table, which is a reading, as for
  `send-routing-info-sm`.
- The PP `pp-data` GET answers `USER_NOT_FOUND`, the only 404 cause 6.5.3.2.3.2 lists, even though the
  detail says that no PP data exists.
- The UDM's 3 501s and 1 503 without a cause were counted, not examined.
- Only the SMSF class is asserted end to end. The other classes were changed mechanically from the
  table above.
- Verified locally only with `-fsyntax-only` on `nfs/udm/src/main.cpp` and the changed test, because
  the self-hosted runners were busy and this host runs CI. The CI result is to be added.
- `specs/3gpp/MANIFEST.tsv` records TS 29.503 j70, which was fetched for this change.
