## ADR-0641: PCF encodes the SUPI in the UDR sm-data path and the serviceParamData query (ADR-0570 class, PCF part)

**Date:** 2026-11-27. **Status:** Proposed.

**Context.** ADR-0570 found that a value taken from a request and put into another NF's URI
unencoded can reach a different resource there (a `/`, `?`, `#` or `%` in it). ADR-0575 listed the
NFs with such sites; the NEF, AUSF, SMF and AMF parts are merged (ADR-0575, ADR-0606, ADR-0603,
ADR-0604, ADR-0605) and bss/provisioning needs none. The PCF was left as "deferred". ADR-0572's
progress table said "the PCF encodes (ADR-0549)"; that is wrong: ADR-0549 is the AAnF's SUPI
encoding, and a grep of `nfs/pcf/src` on `main` 4182d213 finds one site that does not encode and
one that encodes with the wrong function:

1. `fetch_sm_policy_dnn_data` (`nfs/pcf/src/main.cpp`, SmPolicyCreate, ADR-0072) builds
   `{udr}/nudr-dr/v2/policy-data/ues/` + `supi` + `/sm-data` with the raw
   `SmPolicyContextData.supi` from the SMF's body. `TS29571_CommonData.yaml` `Supi` allows
   `nai-.+`, `gci-.+`, `gli-.+` and `.+`, so the value can carry those characters.
2. `fetch_service_params` (`nfs/pcf/src/ue_policy_delivery.cpp`, ADR-0555) builds
   `{udr}/nudr-dr/v2/application-data/serviceParamData?supis=` + the SUPI with
   `encode_path_segment`. `supis` is a query parameter (`TS29519_Application_Data.yaml`,
   `/application-data/serviceParamData`). The path-segment encoder keeps `&`, `=` and `+`
   (RFC 3986 3.3 sub-delims), so a SUPI such as `imsi-...641&supis=imsi-...642` adds a second
   `supis` value and the UDR answers another UE's service parameters too.

Every other PCF URI built from a subscriber id already encodes (`v2x.cpp` UDM `v2x-data` and AMF
`n1-n2-messages`; `ue_policy_delivery.cpp` AMF subscriptions and transfers). The BSF binding id and
the CHF subscription id come from the peer's own `Location` / response body and are used as given,
as in the SMF part (ADR-0604).

**Decision:**

- Site 1 uses `sbi_core::uri::encode_path_segment(supi)` (TS 29.501 5.1.3.2 f), RFC 3986 3.3).
  `main.cpp` now includes `sbi_core/uri.hpp`.
- Site 2 uses `sbi_core::uri::encode_query_value(supi)` (RFC 3986 3.4; the sbi-core helper also
  encodes `&`, `=`, `+`, `;` and `#`).
- An `imsi-<digits>` SUPI is unchanged byte for byte by both encoders, so nothing changes for the
  SUPIs the lab uses. The UDR's sbi_core server decodes the path segment after splitting the path
  (ADR-0549) and percent-decodes query values (`parse_query_string`), so it sees the SUPI the PCF
  was given.
- Tests: `tests/integration/test_pcf_peer_uri_encoding.cpp` in `integration_tests` runs the real
  NRF and PCF against in-process UDR/UDM stand-ins:
  - `PcfPeerUriEncoding.SmPolicySupiWithQueryCharacterReadsOnlyItsOwnSmData`: SmPolicyCreate for
    `imsi-999700000000641?x`; the UDR must see exactly that `{ueId}` on the sm-data route.
  - `PcfPeerUriEncoding.UePolicySupiWithAmpersandIsOneServiceParamDataQueryValue`: UE policy
    Create with V2X negotiated for `imsi-999700000000641&supis=imsi-999700000000642`; the UDR must
    see that whole string as the one `supis` value.

**Rejected alternatives.**

- *Reject a SUPI with reserved characters at the PCF.* The `Supi` pattern allows them for the
  `nai-`/`gci-`/`gli-` forms and the catch-all, so the PCF would refuse valid requests. Encoding
  where the URI is built is what the other parts of the audit did.
- *Use `encode_path_segment` for both sites.* It is the wrong encoder for a query value, which is
  site 2's bug.

**Disclosed.**

- Status of the ADR-0570 audit after this ADR: every NF named in ADR-0575 *Disclosed* is either
  fixed or shown to need nothing. Values derived from a peer's `Location` or response body are
  used as given in all of them (ADR-0604); that is a separate trust question, not covered here.
- ADR-0572's progress table still carries the wrong "the PCF encodes (ADR-0549)" sentence; the
  next docs round corrects it by appending.
