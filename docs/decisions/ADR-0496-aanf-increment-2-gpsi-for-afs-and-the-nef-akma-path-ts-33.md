## ADR-0496: AAnF increment 2 -- GPSI for AFs and the NEF AKMA path (TS 33.535 6.2.1 steps 3-4, 6.3)

**Date:** 2026-10-15. **Status:** Accepted -- built.

**Context.** ADR-0495 left two parts of the approved AKMA procedure list unbuilt: the GPSI for an
AF (TS 33.535 6.2.1 steps 3-4: "if the AAnF determines this specific AF needs GPSI, according to
its local policy, the AAnF sends a Nudm_SDM_Get Request to the UDM to fetch the GPSI") and the AF
outside the operator's network, via the NEF (6.3). `nfs/nef` served TS 29.522's
`/3gpp-akma/v1/retrieve` from the YAML but answered 501 (ADR-0327).

**Decisions.**
1. AAnF: `gpsi_af_fqdns` (local policy) lists the AFs that get the GPSI; for them the AAnF asks the
   UDM (`GET /nudm-sdm/v2/{supi}/id-translation-result`, ADR-0236) and returns the GPSI *instead
   of* the SUPI (6.2.1 step 8: "Whether to send SUPI or GPSI is determined by AAnF based on the local
   policy"). No GPSI available for such an AF: 500 UNSPECIFIED_NF_FAILURE rather than silently
   falling back to the SUPI. Anonymous requests get neither.
2. NEF (6.3): `/3gpp-akma/v1/retrieve` forwards the request to the AAnF
   (Naanf_AKMA_ApplicationKey_Get) with the NEF's own token carrying
   `naanf-akma:applicationkeyget:supi-access`, so the SUPI comes back to the NEF; the NEF answers
   the AF with KAF + expiry and, unless anonInd ("UE Id not needed"), the GPSI translated via the
   UDM -- "The NEF shall not send the SUPI to the AF" (step 5). An AAnF error (e.g. 403
   K_AKMA_NOT_PRESENT) is relayed as-is. `aanf_base_url` in config/nef.json (absent: still 501).

**Disclosed.**
- The NEF does not authorize the AF beyond its existing bearer-token check (6.3 step 2's AF_ID
  verification against the AF's identity is not built), nor relay notifUri (6.8.2 not built).
- The GPSI is fetched per request, not stored in the AKMA context (6.2.1 step 4 says "store").
- The UDM translation returns the first of the subscriber's GPSIs.
- Still not built: roaming check (6.2.1 steps 5-7), AKMA service-disable notification (6.8).

**Verified.** `AkmaEndToEnd.AusfRegistersKakmaAndTheAanfGivesTheAfTheSameKafTheUeDerives`, extended:
an internal AF on the GPSI list gets the GPSI and no SUPI; an external AF through the real NEF gets
the same KAF the UE derives, the GPSI and no SUPI; with anonInd no GPSI; an unknown A-KID through
the NEF gets the AAnF's 403.
