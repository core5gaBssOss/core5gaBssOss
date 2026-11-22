## ADR-0621: SMF ProblemDetails carry invalidParams (SBI-03 step 2, SMF)

**Date:** 2026-11-22. **Status:** Proposed.

**Context.** ADR-0607 (SBI-03 step 1) gave sbi-core a `problem_response` overload that takes an
`InvalidParam` list and four builders for the `param` encodings of `TS29571_CommonData.yaml`
(`InvalidParam.param`: a JSON Pointer for a body attribute, `"query "` plus the name for a query
parameter, `"header "` plus the name for a header, `{name}` for a path variable). TS 29.500
V19.7.0 Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the
'ProblemDetails' data structure indicating unsupported, missing or incorrect IE(s) or query
parameter(s) or 3gpp-Sbi-Discovery-* header(s)." The same table gives `MANDATORY_IE_MISSING` as
"A mandatory IE (within the JSON body or within the variable part of an
"apiSpecificResourceUriPart" or within an HTTP header), or conditional IE but mandatory required,
for an HTTP method is not included in the request. (NOTE 1)" and `MANDATORY_IE_INCORRECT` as "A
mandatory IE (within the JSON body or within a variable part of an "apiSpecificResourceUriPart" or
within an HTTP header), or conditional IE but mandatory required, for an HTTP method was received
with a semantically incorrect value. (NOTE 1)". ADR-0609 (UDR), ADR-0613 (NRF) and ADR-0614 (UDM)
did step 2 for their NFs; this ADR is step 2 for the SMF (`nfs/smf`).

**Decision:** Every SMF site that answers a NOTE 1 cause as a literal now sends invalidParams. A
count on this branch (cut from 55ba4d06) finds 7 such sites, all in `nfs/smf/src/main.cpp` (none
in `ambr.cpp`, `event_subscription_store.cpp`, `li_poi.cpp`, `nas_5gsm_codec.cpp`,
`pfcp_peer.cpp`, `qos_mon_producer.cpp`, `sm_context_store.cpp`, `ue_ip_pool.cpp` or the
headers); none answers a query-parameter cause or `MISSING_PARAMETER`:

| Route (branch) | Cause | invalidParams[].param |
|---|---|---|
| `POST /sm-contexts` (CreateSMContext), supi / pduSessionId / dnn / sNssai absent | `MANDATORY_IE_MISSING` | one entry per absent member: `/supi`, `/pduSessionId`, `/dnn`, `/sNssai` (reason "required by this build for the SM Policy Association") |
| `POST /sm-contexts/{smContextRef}/modify`, PATH_SWITCH_REQ / HANDOVER_REQ_ACK transfer does not decode | `MANDATORY_IE_INCORRECT` | `/n2SmInfo` |
| same route, PDU_RES_SETUP_RSP / PDU_RES_MOD_RSP / PDU_RES_MOD_IND | `MANDATORY_IE_INCORRECT` | `/n2SmInfo` |
| same route, the validate-and-acknowledge types (`kAckOnlyN2SmInfos`) | `MANDATORY_IE_INCORRECT` | `/n2SmInfo` |
| same route, UE_CONTEXT_RESUME_REQ | `MANDATORY_IE_INCORRECT` | `/n2SmInfo` |
| same route, PATH_SWITCH_SETUP_FAIL | `MANDATORY_IE_INCORRECT` | `/n2SmInfo` |
| same route, HANDOVER_RES_ALLOC_FAIL | `MANDATORY_IE_INCORRECT` | `/n2SmInfo` |

- The six `n2SmInfo is not a valid <transfer>` 400s become one helper,
  `n2_sm_info_not_decodable(transfer_name)`, with the same title, detail and cause as before. Its
  entry's reason is "the referenced binary part is not a valid <transfer>".
- Names checked against `specs/5G_APIs-REL-19/TS29502_Nsmf_PDUSession.yaml` (commit
  bca84b60a37773133bcae97e5c6c0d10a93b47b6): `SmContextCreateData.supi`, `.pduSessionId`, `.dnn`,
  `.sNssai`; `SmContextUpdateData.n2SmInfo` (`RefToBinaryData`, `TS29571_CommonData.yaml`) and
  `.n2SmInfoType`.
- No status code, cause, title or detail changed.
- Route order: the SMF registers 13 routes, every one with a distinct literal path or a distinct
  segment count (`/sm-contexts`, `/sm-contexts/{smContextRef}/modify`, `.../retrieve`,
  `.../release`, `.../pcf-notify/update`, `.../pcf-notify/terminate`, `.../send-mo-data`,
  `/pdu-sessions/{pduSessionRef}/transfer-mo-data`, the NIDD deliver route and the four
  event-exposure routes), so no route shadows the two probed here.
- Test: `SmfInvalidParams.*` in `tests/integration/test_smf_invalid_params.cpp`. One probe for the
  create site (supi and dnn absent: `["/supi", "/dnn"]`); one that sends a binary part of four
  0xFF bytes for PDU_RES_SETUP_RSP, PDU_RES_NTY, UE_CONTEXT_RESUME_REQ and PATH_SWITCH_REQ (four of
  the six n2SmInfo sites) and expects `["/n2SmInfo"]`. That `aper_decode_complete` refuses those four
  bytes for these four transfer types was checked with a small C program linked against this
  tree's `libngap_generated.a` (return codes RC_FAIL / RC_WMORE), not assumed.

**Out of scope, unchanged.** The SMF's `parse_json_body` / `parse_multipart_json_body` sites and
the inline `from_json` catch in the multipart UpdateSMContext branch (`dto_error_cause(e)`) still
send no invalidParams, as ADR-0607 disclosed: the nlohmann exception gives no JSON Pointer.

**Disclosed.**
- The CreateSMContext request is multipart/related; the JSON Pointers point into its JSON part
  (`SmContextCreateData`). `TS29571_CommonData.yaml` defines the pointer for "an attribute in a
  JSON body" and does not say how to address a multipart body; pointing into the JSON part is this
  ADR's reading.
- `supi`, `pduSessionId`, `dnn` and `sNssai` are optional in `SmContextCreateData`; the SMF
  requires them (the file-header simplification on the create handler). The cause stays
  `MANDATORY_IE_MISSING`, which the table's "or conditional IE but mandatory required" wording
  covers; whether a 3GPP SMF would reject these requests at all is the existing simplification,
  not changed here.
- `n2SmInfo` is optional in `SmContextUpdateData` but required by the n2SmInfoType it travels
  with, so `MANDATORY_IE_INCORRECT` ("or conditional IE but mandatory required") is kept. The
  incorrect value is the binary part `n2SmInfo` refers to, which has no JSON Pointer of its own;
  `/n2SmInfo` names the member that refers to it.
- The `n2SmInfo` reason does not say which NGAP IE failed to decode; `ngap_per_decode` returns
  only `nullptr`.
- Two of the six n2SmInfo sites, PATH_SWITCH_SETUP_FAIL and HANDOVER_RES_ALLOC_FAIL, have no
  probe; HANDOVER_REQ_ACK, PDU_RES_MOD_RSP, PDU_RES_MOD_IND and the other validate-and-acknowledge
  types reach a probed site but are not sent themselves. All six sites call the same helper.
- Other SMF 400s with a non-NOTE 1 cause (`UNSPECIFIED_MSG_FAILURE` for an n2SmInfoType that
  arrives without its binary part, or a transfer with no IPv4 GTP-U tunnel) are unchanged. An
  absent binary part for a type that needs one is arguably `MANDATORY_IE_MISSING`; this step
  does not change causes.
- `MISSING_PARAMETER` (403) and the query-parameter causes: no SMF site answers them.

**Verified.** See the PR: clang-format-18 and a syntax-only compile of the changed translation
units in this worktree. Not built, and the integration tests were not run here: they need the lab
certificates, which this worktree does not read.

**Verified by the lead, 2026-11-22, main checkout (runner 23 idle).** Tree: this branch at 7c3502cf on 55ba4d06 (#58). Built nrf, upf, smf, pcf and integration_tests. `SmfInvalidParams.*` 2/2 pass. With `nfs/smf/src/main.cpp` restored to 55ba4d06 and rebuilt, both fail. Regression: `--gtest_filter='Smf*:PduSession*:AmfNgapTestGnb.*'` 26/26 pass (247 s). Only these filters were run, not a full ctest.

**Rejected alternatives.**
- *Name all four members on the create 400, as its detail does.* Only the absent ones are wrong;
  naming a present member would tell the consumer to fix something that is fine.
- *Report the undecodable part as `/n2SmInfo/contentId`.* The content ID matched a part; it is the
  part's bytes that are wrong, and `n2SmInfo` is the IE that carries them.
- *Change the cause of the "no binary part" 400s to `MANDATORY_IE_MISSING` here.* A cause change is
  a behaviour change outside this step.
