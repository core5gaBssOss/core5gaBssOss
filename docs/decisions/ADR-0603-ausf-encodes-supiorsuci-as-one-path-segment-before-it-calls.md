## ADR-0603: AUSF encodes supiOrSuci as one path segment before it calls the UDM (ADR-0570 class, AUSF part)

**Date:** 2026-11-21. **Status:** Proposed.

**Context.** This is the AUSF part of ADR-0572 v0.2.0 item 2. ADR-0570 fixed the UDM, ADR-0575 fixed
the NEF's request-path sites, and ADR-0575 *Disclosed* deferred the peer URLs built from a request
body, a stored record or a peer response. For the AUSF it named `:372` and `:771`, both
`supiOrSuci` toward Nudm_UEAU.

An audit of every outgoing `ClientRequest.url` in `nfs/ausf/src` found five sites:

| Site | Value in the URI | Source | Verdict |
|---|---|---|---|
| `main.cpp:120` NRF probe | fixed all-zero UUID | constant | safe |
| `main.cpp:144` NFRegister PUT | `ausf_instance_id` | this NF's own id | safe |
| `main.cpp:263` AAnF `register-anchorkey` | none | constant path | safe |
| `main.cpp` POST `/ue-authentications` → `/nudm-ueau/v1/{supiOrSuci}/security-information/generate-auth-data` | `body->supiOrSuci` | request body | **encoded** |
| `main.cpp` POST `/prose-authentications` → `/nudm-ueau/v1/{supiOrSuci}/prose-security-information/generate-av` | `*body->supiOrSuci` | request body | **encoded** |

The body value is not constrained. `SupiOrSuci` in `TS29571_CommonData.yaml` has the pattern
`'^(imsi-[0-9]{5,15}|nai-.+|gli-.+|gci-.+|suci-(...)|.+)$'`, and its last alternative `.+` admits
any string, `/`, `?`, `#` and `%` included. The sbi_core server splits `:path` at the first `?`
(`http2_server.cpp`). So, for example, the value
`imsi-999700000000001/security-information/generate-auth-data?` made the AUSF POST to that
subscriber's own generate-auth-data path, with the rest of the string as a query. The AUSF then
authenticated a subscriber the request had not named as its identity.

**Decision:**

- Both UDM sites put the value through `sbi_core::uri::encode_path_segment`, the helper of
  ADR-0570/ADR-0575 (TS 29.501 5.1.3.2 f), corrected on 2026-11-21 from a wrong "5.2.3"; RFC 3986 3.3). Any SUPI that matches the specific
  alternatives (`imsi-<digits>`, `nai-user@realm`, a `suci-...` of hex digits and dashes) is
  unchanged byte for byte: every character in it is unreserved or `@`. Only values with characters
  outside RFC 3986 `pchar` change on the wire, and the UDM's server decodes them back after it
  splits the path.
- Test: `AusfIntegration.SupiOrSuciWithPathCharactersCannotReachAnotherSubscriber` in
  `tests/integration/test_ausf_ue_authentication.cpp` sends the value above with real NRF, UDM,
  UDR and AUSF processes. It asserts the answer is not 201 but 404 `USER_NOT_FOUND`, because the
  UDM sees one unknown id. Without the fix the UDM serves the seeded subscriber's vectors and the
  AUSF answers 201.

**Verified (2026-11-21, master machine, main checkout at this commit).** `ausf`, `udm`, `udr`,
`nrf` and `integration_tests` build clean (0 failed steps). With Valkey and the UDR Postgres up,
`AusfIntegration.SupiOrSuciWithPathCharactersCannotReachAnotherSubscriber` passes (404
`USER_NOT_FOUND`). With `nfs/ausf/src/main.cpp` restored from 16b5e7fd and `ausf` rebuilt, it fails:
the AUSF answers **201** and starts authenticating the seeded subscriber, so the bug was real.
Targeted test only, not a full `ctest`. CI on PR #54 (run 44912960619, rebased onto ea82f83f) passed:
build, lint, asan-ubsan and tsan (2026-11-21). Merged as d55cc7b0.

**Disclosed.**
- Only the 5G AKA / EAP-AKA' path has a test. The ProSe site gets the same call with the same
  helper, but no test drives it.
- No validation is added: a value that does not match the specific alternatives is still sent to
  the UDM, now encoded, as before. Refusing it with 400 would narrow the YAML's own `.+`
  alternative.

**Rejected alternatives.**
- *Reject `supiOrSuci` values with `/`, `?`, `#` or `%` with 400.* The YAML allows them (`.+`), so a
  refusal would be stricter than the spec. ADR-0570 rejected the same at the UDM: a valid `nai-`
  SUPI could be refused. Encoding keeps the value and removes the confusion.
- *Encode in the sbi_core client for every URL.* The client cannot tell a path separator the
  caller meant from one a value brought in.
