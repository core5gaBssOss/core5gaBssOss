## ADR-0604: SMF encodes the SUPI as one path segment before it calls the AMF (ADR-0570 class, SMF part)

**Date:** 2026-11-21. **Status:** Proposed.

**Context.** ADR-0570 fixed the UDM and deferred an audit of the other NFs that build a peer URI
from a value they did not choose. ADR-0603 did the AUSF. This ADR does the SMF. ADR-0549 made the
sbi_core server percent-decode path parameters, so a value that holds `/`, `?`, `#` or `%` must be
encoded again before it goes into the next hop's URI.

All 15 `ClientRequest` URLs that `nfs/smf/src` builds were read (all in `main.cpp`). Two of them put
a SUPI into the AMF path `/namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages`
(`TS29518_Namf_Communication.yaml`):

- the N1N2MessageTransfer of the PDU Session Establishment Accept in the CreateSMContext handler
  (`*body->supi`, straight from the SmContextCreateData body);
- `send_release_command_via_amf` (the `supi` stored in the SM context at create time, so the same
  body value).

The TS29571 `Supi` pattern ends in `|.+`, so such a SUPI passes the schema. Example:
`supi = "imsi-999700000000001/n1-n2-messages?x="` made the SMF POST to
`/namf-comm/v1/ue-contexts/imsi-999700000000001/n1-n2-messages?x=/n1-n2-messages`, which is
another UE's N1N2MessageTransfer resource. The Accept (or Release Command) would be delivered to
that UE's context.

**Decision:**

- Both AMF URLs encode the SUPI with `sbi_core::uri::encode_path_segment` (`sbi_core/uri.hpp`, from
  ADR-0570), where the URL is built. The stored and logged SUPI stays unencoded.
- Test: `SmfIntegration.SupiWithPathCharactersCannotReachAnotherUesAmfContext` in
  `tests/integration/test_smf_pdu_session.cpp`. It runs a real NRF, SMF and PCF. The AMF is an
  in-process `sbi_core::http2::Server` on port 7778 (the `amf_base_url` in `config/smf.json`),
  registered at the NRF as an AMF so that the NRF issues the SMF a `namf-comm` token. The test
  sends CreateSMContext with the SUPI above and an n1SmMsg that holds a PDU Session Establishment
  Request header. It then asserts that the AMF received exactly one N1N2MessageTransfer, that its
  `ueContextId` is not `imsi-999700000000001`, and that it is the whole SUPI.

**Left as they are (and why).**

- Callback URIs supplied by consumers (`smContextStatusUri`, QoS monitoring `notifUri`) are used as
  given, as in ADR-0570.
- `chargingDataRef` (CHF Release/Update URLs) and `smPolicyId` (PCF DeleteSMPolicy URL) are the last
  segment of the peer's own `Location` header. They are already in URI form, so encoding them again
  would turn a peer's `%xx` into `%25xx` and break the reference. They are not request-derived.
- The NRF URLs use the SMF's own instance id from configuration. The UDR influenceData, NSACF, CHF
  create and PCF create URLs are constants. The PCF `notificationUri` carries the SMF's own
  `smContextRef`.

**Rejected alternatives.**

- *Rejecting a SUPI that contains `/` or `?` in CreateSMContext.* TS 29.571 allows `nai-` and other
  SUPI forms with any character, which is why ADR-0549 decodes and ADR-0570 encodes.
- *Encoding the SUPI when it is stored.* The SUPI is also a log field, the PCF and CHF body value
  and the NSACF body value, all of which need it unencoded. The same reason as ADR-0570.

**Disclosed.**

- Only the SMF is changed. Other NFs still need the same audit.
- The test covers the CreateSMContext site only. `send_release_command_via_amf` uses the same
  helper on the same value and has no test of its own.
- A `chargingDataRef` or `smPolicyId` whose `Location` carried a query or fragment would keep it.
  That is a malformed peer answer, not a request-derived value, and is not handled here.

**Verified (2026-11-21, this machine, worktree build).**

- `smf` and `integration_tests` build clean (Debug, 2768 of 2768 steps, 0 failed).
  `nfs/smf/src/main.cpp` and the test file are clang-format-18 clean. `integration_tests
  --gtest_list_tests` lists the new test.
- Not run here: the test needs `certs/`, which this worktree does not have. It has not yet been
  shown to pass with the fix or to fail without it. To be run in the main checkout
  (`--gtest_filter='SmfIntegration.SupiWithPathCharactersCannotReachAnotherUesAmfContext'`), once
  with this commit and once with `nfs/smf/src/main.cpp` reverted. CI result to be added.
