## ADR-0498: TSCTSF increment 1 -- Ntsctsf_QoSandTSCAssistance for AF requested QoS, PCF binding registration, NEF GPSI routing (TS 29.565)

**Date:** 2026-10-15. **Status:** Accepted -- built (NF and procedure list chosen by the user, 2026-10-15).

**Context.** Next pipeline NF (user's choice). Besides the NF itself it fixes the non-conformance
disclosed in ADR-0487: TS 29.522 4.4.9.3 -- for AF requested QoS for a UE identified by GPSI (no UE
address) "No direct interactions between the NEF and the PCF shall take place"; the NEF goes
through the TSCTSF. Specs: TS 29.565 j70 (fetched), TS29565_Ntsctsf_QoSandTSCAssistance.yaml
(REL-19; added to codegen), TS 29.521 (BSF), TS 29.514 (PCF), TS 29.505 (identity data).

**Approved procedure list (increment 1).** Ntsctsf_QoSandTSCAssistance Create (5.3.2.2.2 by UE
IPv4 address; 5.3.2.2.8 by GPSI with the GMEC feature), Update (5.3.2.3, flows / QoS reference),
Delete (5.3.2.4), read; NEF routing of GPSI-only AsSessionWithQoS through it.

**Decisions.**
1. `nfs/tsctsf` (port 7825, metrics 9497), API root `{apiRoot}/ntsctsf-qos-tscai/v1`. Create
   resolves the PDU session through the BSF (Nbsf_Management_Discovery by `ipv4Addr`, or for GMEC by
   the SUPI the UDM returns for the GPSI, + DNN + S-NSSAI) and creates an AF session at that PCF
   (Npcf_PolicyAuthorization_Create: ueIpv4, supi, gpsi, dnn, sliceInfo, one media component with
   the AF's flows and qosReference). No binding: 500 PDU_SESSION_NOT_AVAILABLE; PCF 403: 403
   REQUESTED_SERVICE_NOT_AUTHORIZED (table 6.2.7.3-1). Update re-provisions the media component at
   every AF session (PATCH); Delete removes them.
2. **PCF registers PDU session bindings at the BSF** (Nbsf_Management_Register on SM policy creation
   -- SUPI, GPSI, UE IPv4, DNN, S-NSSAI, its IP endpoint -- and Deregister on deletion). Nothing in the
   repo did this, so no NF could find the PCF of a UE; `bsf_base_url` in config/pcf.json.
3. **UDM GPSI -> SUPI** in Nudm_SDM id-translation-result through the UDR's identity data
   (`/subscription-data/{gpsi}/identity-data`, supiList), closing the gap ADR-0236 disclosed.
4. NEF: an AsSessionWithQoS with a GPSI and no UE address (ueIpv4Addr / ueIpv6Addr / macAddr) goes to
   the TSCTSF (suppFeat GMEC); revoke deletes it there. With a UE address the NEF still uses the PCF
   directly, which 4.4.9.2 allows. `tsctsf_base_url` in config/nef.json.

**Disclosed.**
- The GMEC association of TS 29.565 is driven by PCF notifications of new PDU sessions (5.3.2.2.8
  NOTE 1); this PCF sends none, so the TSCTSF uses the bindings that exist when the request arrives
  (NOTE 2's BSF retrieval). A PDU session established later is not associated. The BSF returns one
  binding per query, so one AF session per request.
- IPv4 PDU sessions only (bindings and lookups); no external groups, no Ethernet.
- Not built: TSC assistance container / 5GS delay / BAT (tscQosReq not acted on), alternative QoS,
  event subscriptions and notifications towards the consumer (the PCF's notifications to the TSCTSF
  are accepted and dropped), sponsored connectivity, Ntsctsf_TimeSynchronization and _ASTI.
- TSCTSF contexts are in memory (one replica in deploy/helm/tsctsf).

**Rejected alternatives.** The NEF storing the request in the UDR as Application Data (the other
branch of 4.4.9.3): it needs the PCF to consume that data on PDU session establishment, which it does
not. A TSCTSF that only stores requests without reaching a PCF: an accepted QoS request with no
effect.

**Verified.** `TsctsfEndToEnd.GpsiOnlyQosRequestReachesThePcfOfThePduSessionThroughTheTsctsf` (real
NRF, UDR, UDM, BSF, PCF, TSCTSF, NEF: GPSI identity data provisioned at the UDR; the test as SMF
creates an SM policy, the PCF's binding appears at the BSF; the AF's GPSI-only request through the
NEF creates an AF session at the PCF with the UE's IPv4, SUPI, GPSI and qos-gold; NEF revoke deletes
it (PCF 404); TSCTSF by UE address 201; GPSI without GMEC 400; after the SM policy is deleted 500
PDU_SESSION_NOT_AVAILABLE).

**Review fixes (2026-10-15, independent review).** (1) `qosReference` is required in
TscAppSessionContextData: the TSCTSF answers 400 MANDATORY_IE_MISSING without it (and without
tscQosReq), and the NEF refuses a GPSI-only AsSessionWithQoS without one -- before, an empty
reference installed an AF session with no QoS and the AF got 201. (2) On the TSCTSF path the NEF
now relays the TSCTSF's failure to the AF and drops the subscription instead of answering 201 for
nothing installed (the older direct-PCF path keeps its existing behaviour, disclosed in ADR-0315).
(3) The PCF's port parsing of `self_base_url` no longer throws on a URL without a port. Verified: the
TSCTSF e2e now covers _Update (PATCH re-provisions qos-bronze at the PCF) and the missing
qosReference 400.

**Correction (2026-10-16).** The review-fix paragraph above claimed the e2e covered _Update; it did
not pass. The PCF's Npcf_PolicyAuthorization ModAppSession merged the whole request body into
`ascReqData`, but the body is `AppSessionContextUpdateDataPatch` (`{"ascReqData": ...}`,
TS29514_Npcf_PolicyAuthorization.yaml), so the TSCTSF's update landed at
`ascReqData.ascReqData` and the media component kept qos-silver. Pre-existing PCF bug, exposed only
now because the TSCTSF is its first ModAppSession client (the NEF only creates and deletes). The PCF
now merges the body's `ascReqData` member and answers 400 to a non-object body. Verified:
`TsctsfEndToEnd.GpsiOnlyQosRequestReachesThePcfOfThePduSessionThroughTheTsctsf` passes, qos-bronze
at the PCF after the PATCH.
