## ADR-0605: AMF encodes the SUPI as one path segment in the URIs it builds (ADR-0570 class, AMF part)

**Date:** 2026-11-21. **Status:** Proposed.

**Context.** This is the AMF part of the audit ADR-0570 deferred (ADR-0572 v0.2.0 item 2). ADR-0575
*Disclosed* named one AMF site, `ngap_task.cpp:1644` (`supi`, Nsmsf_SMService). ADR-0603 fixed the
AUSF. The AMF's SBI server handlers call no peer (the only `ClientRequest` in `main.cpp` is the NRF
probe and NFRegister); every peer call is in `ngap_task.cpp` and `ngap_handover.cpp`, driven by
NGAP/NAS. An audit of every outgoing URL in `nfs/amf/src` (line numbers before this change):

| Site | Value in the URI | Source | Verdict |
|---|---|---|---|
| `main.cpp:180` NRF probe | fixed all-zero UUID | constant | safe |
| `main.cpp:205` NFRegister PUT | `amf_instance_id` | this NF's own id (config) | safe |
| `ngap_task.cpp:651` Nausf_UEAuthentication POST | none | constant path | safe (SUPI is in the body) |
| `ngap_task.cpp:1428` 5G AKA confirmation PUT | `confirmation_path` | AUSF's `_links["5g-aka"].href` | left: a link the AUSF chose, used as given |
| `ngap_task.cpp:1575` Nnsacf_NSAC POST | none | constant path | safe (SUPI is in the body) |
| `ngap_task.cpp:1644` Nsmsf_SMService Activate PUT `/ue-contexts/{supi}` | `supi` | NAS RegistrationRequest / stored context | **encoded** |
| `ngap_task.cpp:1922` Npcf_AMPolicyControl Create POST | none | constant path | safe |
| `ngap_task.cpp:2052` Nsmsf_SMService UplinkSMS POST `/ue-contexts/{supi}/sendsms` | `auth_state.supi` | same | **encoded** |
| `ngap_task.cpp:2255` `smContextStatusUri` sent to the SMF | `auth_state.supi` | same | **encoded** |
| `ngap_task.cpp:2129`, `:2516`; `ngap_handover.cpp:188`, `:321`, `:1102`, `:1304` SM context modify/release | `sm_context_ref` | last segment of the SMF's `Location` | left (see Disclosed) |
| `ngap_task.cpp:2553` AM policy association DELETE | `pol_asso_id` | last segment of the PCF's `Location` | left (see Disclosed) |

The SUPI here is not only a provisioned value. `nas_codec.cpp` builds it as `"imsi-" + MCC + MNC +
MSIN` from the 5GS mobile identity, and `bcd_digit` writes `?` for a BCD nibble 0xA..0xE, so a UE
can put a `?` in the SUPI the AMF uses. Pasted into `PUT /nsmsf-sms/v2/ue-contexts/{supi}`, a `?`
ends the path, and the SMSF activates SMS over NAS for the subscriber named before it. The
`smContextStatusUri` goes to the SMF and comes back to the AMF's own route
`/namf-callback/v1/sm-context-status/{supi}/{pduSessionId}`; an unencoded `/` there would point the
SMF's release notification at another UE's PDU session. In practice the SUPI first has to pass 5G
AKA (a provisioned subscriber with that id), so this is defence in depth rather than a reachable
cross-subscriber path today; it is fixed because the class is the same as ADR-0570 and ADR-0603.

**Decision:**

- The three subscriber-derived URIs are built by header-only helpers in
  `nfs/amf/src/peer_uris.hpp` (`smsf_ue_context`, `smsf_send_sms`, `sm_context_status_callback`),
  each putting the SUPI through `sbi_core::uri::encode_path_segment` (TS 29.501 5.1.3.2 f), RFC 3986
  3.3). The value is encoded where the URI is built; the SUPI stays raw as the local key, in logs
  and in JSON bodies. An `imsi-<digits>` or `nai-user@realm` SUPI is unchanged byte for byte. The
  SMSF's and the AMF's own sbi_core server decode the segment back after splitting the path
  (ADR-0549), so the callback route gets the same SUPI it stored.
- Paths are the YAML's: `TS29540_Nsmsf_SMService.yaml` `/ue-contexts/{supi}` and
  `/ue-contexts/{supi}/sendsms`. The callback path is the AMF's own (ADR-0523), not a spec path.
- Test: `AmfPeerUriEncoding.SupiWithQueryCharacterCannotActivateSmsForAnotherSubscriber` in
  `tests/integration/test_amf_peer_uri_encoding.cpp`, in `integration_tests`. It runs the real NRF
  and SMSF, calls the AMF's `smsf_ue_context` builder for `imsi-999700000000605?` and sends the
  Activate PUT the AMF sends. It expects 201, no SMS context for `imsi-999700000000605` (PATCH
  `[]` answers 404) and a context under the whole SUPI (204). It also pins the three builders'
  output for values with `/` and `?`. Without the fix the PUT lands on `imsi-999700000000605` and
  the 404 assertion fails. The header is made visible to this one source file only
  (`set_source_files_properties(... INCLUDE_DIRECTORIES ...)`), so no other test in the target can
  pick up an AMF-private header.

**Verified (2026-11-21, master machine, fresh worktree build).** Only what is listed here was run.

- `cmake --build build --target amf integration_tests` completed; a second `ninja -n` on the same
  targets reports no work to do. `integration_tests --gtest_list_tests` lists
  `AmfPeerUriEncoding.SupiWithQueryCharacterCannotActivateSmsForAnotherSubscriber`.
- `clang-format-18 --dry-run -Werror` is clean on every changed C++ file.
- The test was **not** run here: it needs `certs/`, which this worktree may not read. The lead runs
  `--gtest_filter=AmfPeerUriEncoding.*` in the main checkout, plus the revert check (restore
  `nfs/amf/src/peer_uris.hpp`'s two `encode_path_segment(supi)` calls to plain `std::string(supi)`
  and expect the victim assertion to fail). Not a full `ctest`. CI result to be added.

**Disclosed.**

- The test drives the AMF's builder against the real SMSF, not the AMF binary end to end. An end
  to end run would need a RegistrationRequest with a 0xA..0xE MSIN nibble, a UDR subscriber with
  that SUPI, and the AUSF fix of ADR-0603 (PR #54, not yet on main) for 5G AKA to reach the UDM
  with it; the test UE builder (`ue_nas_driver`) encodes decimal digits only. `smsf_send_sms` and
  `sm_context_status_callback` are pinned by string assertions only; no request is sent on them.
- SM context refs and the PCF policy association id are taken as the text after the last `/` of
  the peer's `Location` header, so they are already in URI form and contain no `/`. Encoding them
  again would turn a `%` the peer wrote into `%25`. A `?` from a misbehaving SMF or PCF would still
  pass through; parsing `Location` as a URI is a separate change, deferred for future
  implementation.
- The AUSF's `5g-aka` href is used as the AUSF gave it, like consumer-supplied callback URIs.
- `Location` headers the AMF returns (`main.cpp`, e.g. `/ue-contexts/` + `ue_context_id`) are built
  from decoded path values without encoding. They are responses, not peer calls, and are not
  changed here; deferred for future implementation.
- `bcd_digit` still maps an invalid BCD nibble to `?` instead of rejecting the RegistrationRequest.
  Whether to reject it (and with which 5GMM cause) is a TS 24.501 question not decided here.

**Rejected alternatives.**

- *Reject a SUPI with `/`, `?`, `#` or `%` at the AMF.* TS 29.571 lets a SUPI (`nai-...`) carry
  them, as ADR-0570 and ADR-0603 found; encoding keeps the value and removes the confusion.
- *Encode `sm_context_ref` and `pol_asso_id` too.* They come out of a URI already encoded; a second
  encoding would corrupt any `%` in them.
- *Encode in the sbi_core client for every URL.* The client cannot tell a separator the caller
  meant from one a value brought in.
