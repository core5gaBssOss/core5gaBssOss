## ADR-0495: AAnF increment 1 -- AKMA anchor key, application key and context removal (TS 33.535, TS 29.535), with the AUSF and UDM sides

**Date:** 2026-10-15. **Status:** Accepted -- built (NF and procedure list chosen by the user, 2026-10-15).

**Context.** Next pipeline NF after the SEPP (ADR-0491..0494), chosen by the user among AAnF,
NSSAAF, TSCTSF and EASDF. Specs: TS 33.535 j00 (fetched), TS 29.535 j50 (fetched),
TS29535_Naanf_AKMA.yaml (REL-19; added to codegen) with bodies from TS29522_AKMA.yaml, TS 29.503 /
TS29503_Nudm_UEAU.yaml (akmaInd, routingId), TS 29.505 (AuthenticationSubscription.akmaAllowed,
routingId), TS 33.220 Annex B.2.2 KDF, TS 23.003 j80.

**Approved procedure list (increment 1).**
- TS 33.535 6.1 (KAKMA after primary authentication): UDM returns akmaInd + routingId when the
  UDR subscription has akmaAllowed; after a successful 5G-AKA or EAP-AKA' confirmation the AUSF
  derives KAKMA (A.2, FC 0x80) and the A-TID (A.3, FC 0x81) from KAUSF and the SUPI, builds the
  A-KID, and registers both at the AAnF (Naanf_AKMA_AnchorKey_Register, POST
  /naanf-akma/v1/register-anchorkey). A new registration for a SUPI replaces the old A-KID (NOTE 1a).
- 6.2.1 / 6.2.2 / 6.3 (KAF for an AF, also anonymous / via NEF): POST /retrieve-applicationkey ->
  KAF = KDF(KAKMA, AF_ID) (A.4, FC 0x82), expiry, SUPI unless anonInd or not authorized.
- 6.6 context removal: POST /remove-context -> 204, 404 AKMA_CONTEXT_NOT_FOUND.

**Decisions.**
1. `nfs/aanf` (port 7824, metrics 9496), contexts in Valkey (`aanf:akid:<A-KID>`,
   `aanf:supi:<SUPI>`), NRF registration as nfType AANF; "AANF" added to the User-Agent NF types
   (ADR-0490). `config/aanf.json`: KAF lifetime, `supi_to_af_without_token`, `allowed_af_fqdns`.
2. **A-KID format (user decision, 2026-10-15).** TS 33.535 j00 says only "username includes the RID
   and the A-TID, realm the Home Network Identifier"; TS 23.003 j80 has no A-KID clause. By the
   user's choice it mirrors TS 23.003 28.7.11 (CP-PRUK ID):
   `rid<RID>.tid<A-TID as 64 lowercase hex>@5gc.mnc<MNC>.mcc<MCC>.3gppnetwork.org`
   (`aka_crypto::make_a_kid`, one place). An interpretation pending a spec text that defines it --
   a UE built to another reading would derive the same KAKMA but present a different A-KID.
3. AF_ID: TS 29.522's AfId "<FQDN>.<Ua* security protocol id, 10 hex digits>" ->
   FQDN || 5 protocol octets as the KDF input (`aka_crypto::af_id_octets`).
4. RID: the subscription's routingId, else "0" (TS 23.003 2.2B default). SUPI KDF input: the bare
   identity (as for KAMF, TS 33.535 A.2 -> TS 33.501 A.7.0).
5. SUPI to the AF: with a token only if its scope has `naanf-akma:applicationkeyget:supi-access`
   (the YAML's scope); without a token by local policy (default no).
6. The AUSF's AKMA configuration (`akma` in config/ausf.json): the AAnF URL and its home PLMN for
   the A-KID realm (999/70, the lab subscribers' PLMN). Without it, no AKMA keys are derived.

**Disclosed.**
- kAkma / kaf are hexadecimal: the specs type them "string" without an encoding; hex follows
  TS 29.509's key fields.
- Not built: the roaming check (6.2.1 steps 5-7, Nudm_EventExposure RoamingStatusReport;
  RoamingRestriction feature), GPSI (steps 3-4; AKMA_GPSI_Support), the AKMA service-disable
  notification (6.8; notifUri accepted and ignored), AAnF discovery via the NRF (the AUSF uses a
  configured URL), KAKMA/KAF re-keying beyond "new primary authentication replaces" (6.4), and the
  NEF's AKMA API (TS 29.522 3gpp-akma) -- the NEF path of 6.3 is served by the AAnF but no NEF calls
  it.
- Registration happens synchronously in the confirmation handler; its failure is only logged (the
  authentication result stands).
- The UDM's LI UEAuthenticationResponse keeps its aKMAIndicator absent: TS 33.128 ties it to AKMA
  being requested in the AuthenticationInfoRequest, which has no such field.

**Rejected alternatives.** A-KID with the A-TID base64url-encoded or untagged: the user chose the
CP-PRUK-style form (decision 2). Storing contexts in memory: replicas would answer differently.
Deriving the realm from the SUPI digits: a 2- vs 3-digit MNC cannot be told from an IMSI.

**Verified.** `AkmaEndToEnd.AusfRegistersKakmaAndTheAanfGivesTheAfTheSameKafTheUeDerives` (real
NRF, UDR, UDM, AUSF, AAnF: akmaAllowed provisioned over Nudr; 5G-AKA; the AAnF's KAF equals the
UE-side derivation; SUPI with the supi-access scope; anonymous without SUPI; unknown A-KID 403
K_AKMA_NOT_PRESENT; removal 204 then 404 AKMA_CONTEXT_NOT_FOUND).
