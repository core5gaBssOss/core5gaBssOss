## ADR-0551: PCF V2X/A2X UE policy provisioning (P1-P7)

**Date:** 2026-11-01. **Status:** Proposed.

**Context.** ADR-0540 puts V2X (TS 23.287) and A2X (TS 23.256) UE policy provisioning in scope for the PCF. The user approved procedure list P1-P7 on 2026-11-01. Today the PCF's Npcf_UEPolicyControl only stores the association and echoes the consumer's `suppFeat`; it hands back no policy content (ADR-0204).

Stage-3 sources, all in hand: TS 29.525 j70, TS29525_Npcf_UEPolicyControl.yaml and TS29503_Nudm_SDM.yaml (REL-19), TS 29.571, TS 24.587 j30, NGAP Rel-17 ASN.1 (`specs/NGAP/ngap-17.9.asn`).

**Approved procedure list.**

| # | Procedure | Reference |
|---|---|---|
| P1 | UE Policy Association Create stores `pc5Capab` / `a2xCapab`; eligibility from UDM `/{supi}/v2x-data` and `/{supi}/a2x-data` | TS 23.287 §6.2.2, TS 23.256 §6.2.7.1, TS 23.502 §4.16.11, TS 29.525 §4.2.2.2 |
| P2 | AMF-initiated Modification (`/policies/{polAssoId}/update`) carrying a changed `pc5Capab`/`a2xCapab`, or `PLMN_CH` when the "PlmnChange" feature is supported: the PCF re-determines the V2XP/A2XP and N2 PC5 policy | TS 23.502 §4.16.12.1, TS 29.525 §4.2.3.1, §4.2.3.2 NOTE 1 |
| P3 | Read UDR application data `paramOverPc5`, `paramOverUu`, `a2xParamsPc5`, `a2xParamsUu`, and subscribe to changes | TS 29.519 §6.2.15 |
| P4 | Manage UE policy command with a V2XP / A2XP part, via N1N2MessageTransfer, N1 class `UPDP` | TS 29.525 §4.2.2.2.4, §4.2.2.2.6; TS 24.587; TS 24.588 §5.3 |
| P5 | V2X N2 PC5 policy via Namf_Communication_N1N2MessageTransfer | TS 29.525 §4.2.2.3, §4.2.2.5 |
| P6 | UE-triggered V2X/A2X policy provisioning via N1MessageNotify | TS 24.587 §5.3.2 (V2X), TS 24.577 §5.3.2 (A2X); see ADR-0563 |
| P7 | PCF-initiated Modification and re-delivery | TS 23.502 §4.16.12.2, TS 29.525 §4.2.4.5 (V2X), §4.2.4.8 (A2X) |

**Decision:**

1. **Three increments, one PR each.** Increment 1 = P1 + P2 + P5; increment 2 = P3 + P4; increment 3 = P6 + P7. Each row gets a test derived from its call flow.
2. **P5 uses N1N2MessageTransfer in the non-roaming case too.** TS 29.525 §4.2.2.3 says: "In the roaming or non-roaming case, the (V-)PCF shall use the Namf_Communication_N1N2MessageTransfer service operation defined in clause 5.2.2.3.1 of 3GPP TS 29.518 [14] to send V2X N2 PC5 policy to the NG-RAN." §4.2.2.5 says the same for A2X. `n2Pc5Pol` / `n2Pc5PolA2x` in the association body are only H-PCF → V-PCF. The PCF posts `n2InfoContainer{n2InformationClass: V2X, v2xInfo{n2Pc5Pol{ngapIeType: PC5_QOS_PARA, ngapData}}}` with an `application/vnd.3gpp.ngap` part carrying an NGAP `PC5QoSParameters` value (same multipart pattern as the SMF's N1N2MessageTransfer).
3. **Eligibility.** The PCF determines V2X policy only when the UE reports `pc5Capab` and the `V2X` feature is negotiated (TS 29.525 §4.2.2.2, line 467 of the j70 text). It then GETs the UDM `v2x-data` and treats the UE as eligible if any of `nrV2xServicesAuth` / `lteV2xServicesAuth` has `vehicleUeAuth` or `pedestrianUeAuth` = `AUTHORIZED`. A2X likewise with `a2xCapab`, `a2x-data` and `uavUeAuth`.
4. **Feature negotiation (TS 29.500 §6.6).** `suppFeat` in the Create response becomes the intersection of the consumer's features and the PCF's own, instead of an echo. The PCF supports `PlmnChange` (feature 2), `V2X` (feature 4) and `A2X` (feature 15) per TS 29.525 §5.8; feature n is bit n-1 of the hex string, counted from its last character.
5. **Configuration, not code.** `amf_base_url`, `udm_base_url` and the PC5 QoS flow list (PQI, bit rates, range) come from `config/pcf.json`. Without `amf_base_url` / `udm_base_url` the PCF skips V2X provisioning and logs that it did.

**Disclosed (deferred for future implementation).**

- **A2X N2 PC5 policy encoding.** NGAP Rel-17 has no A2X PC5 type, so the PCF works out A2X eligibility in increment 1 but sends no A2X N2 policy. Deferred until the Rel-18/19 NGAP ASN.1 is in tree.
- **The H-PCF → V-PCF relay** of `n2Pc5Pol` / `n2Pc5PolA2x` in roaming.
- **Optional NGAP IEs** beyond the configured flow list.
- **AMF discovery.** The AMF is a static `amf_base_url`, not NRF discovery or the `notificationUri` / AMF from the association.
- **The `range` mapping** from the TS 29.571 `Uinteger` (metres) to the NGAP `Range` enum covers the enumerated values only. A configured `range_m` that is not one of them is a configuration error: `parse_pc5_config` rejects it at startup, and the PCF then runs with no PC5 QoS flows (V2X-eligible UEs get no N2 PC5 policy) and logs why.
- **`UE_CAP_CH` is not used for V2X/A2X.** TS 29.525 §4.2.3.2 NOTE 6 ties that trigger to the "ProSe" feature (5G ProSe capabilities). P2 instead relies on §4.2.3.1: an Update that carries `pc5Capab`/`a2xCapab` makes the PCF re-determine the policy, with no trigger subscription.
- **Increments 2 and 3** (P3, P4, P6, P7) are not in increment 1's PR.
- **The default PC5 QoS flow is PQI 55**, taken from TS 23.287 Table 5.4.4-1 (`23287-j10`): Non-GBR, default priority level 3, packet delay budget 10 ms, packet error rate 10^-4, example service "Cooperative lane change – higher degree of automation". It is a lab default in `config/pcf.json`, not an operator recommendation; GBR and Delay Critical GBR PQIs are unicast-only (Table NOTE 1).
- **`encode_path_segment` was duplicated (resolved 2026-11-03).** `nfs/pcf/src/v2x.cpp` carried its own RFC 3986 path-segment encoder for the SUPI in the UDM and AMF URLs while the shared `sbi_core::uri::encode_path_segment` (ADR-0549, SEC-15) was not on main. #79 merged it, and the PCF now uses the shared encoder; the copy is removed. Still open: the `supis` query parameter of the UDR serviceParamData read (`ue_policy_delivery.cpp`) is encoded with the same path-segment encoder, which keeps `&`, `=` and `+`. That is harmless for an `imsi-` SUPI but not safe for a `nai-` SUPI containing those characters. `sbi_core::uri` has no query-component encoder yet, so this is a known non-conformance, not fixed here.
- **Outbound calls run on one bounded worker** (`pcf::BoundedWorker`, 1024 jobs). When the queue is full the PCF logs it and skips that UE's V2X provisioning; the policy association itself is still answered. Jobs still queued at shutdown are dropped.

**Rejected alternatives.**

- **`n2Pc5Pol` in a non-roaming Create response.** TS 29.525 §4.2.2.3 gives that IE to the H-PCF → V-PCF leg only; the non-roaming PCF must use N1N2MessageTransfer.
- **Reusing `PC5_QOS_PARA` for A2X.** That NGAP type is the V2X one; filling A2X into it would invent an encoding.
- **Hard-coding the PC5 QoS flows.** A change of PQI or bit rate would need a recompile (guardrail 8).
