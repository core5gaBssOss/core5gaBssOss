## ADR-0607: sbi-core ProblemDetails carry invalidParams (SBI-03 step 1, TS 29.500 Table 5.2.7.2-1 NOTE 1)

**Date:** 2026-11-21. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SBI-03 lists `invalidParams` (NOTE 1) as open. TS 29.500 V19.7.0
(`specs/3gpp/29500-j70.docx`), clause 5.2.7.2, Table 5.2.7.2-1, says:

> NOTE 1: "invalidParams" attribute shall be included in the "ProblemDetails" data structure
> indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
> 3gpp-Sbi-Discovery-* header(s).

The rows whose description carries "(NOTE 1)" are `INVALID_QUERY_PARAM`,
`MANDATORY_QUERY_PARAM_INCORRECT`, `OPTIONAL_QUERY_PARAM_INCORRECT`,
`MANDATORY_QUERY_PARAM_MISSING`, `MANDATORY_IE_INCORRECT`, `OPTIONAL_IE_INCORRECT`,
`MANDATORY_IE_MISSING` (all 400) and `MISSING_PARAMETER` (403). Clause 5.2.9 b) adds, for an
unsupported query parameter on a non-safe method, a 400 with cause `INVALID_QUERY_PARAM` and "the
invalidParams attribute indicating the unsupported query parameters".

The shapes are in `TS29571_CommonData.yaml` (REL-19, commit
bca84b60a37773133bcae97e5c6c0d10a93b47b6): `ProblemDetails.invalidParams` is an array of
`InvalidParam` with `minItems: 1`; `InvalidParam` has `param` (string, required) and `reason`
(string). The description of `param` fixes four encodings: a JSON Pointer for a body attribute,
`"header "` plus the name for an HTTP header, `"query "` plus the name for a query parameter, and
the variable name including `{` and `}` for a path variable. `sbi_core::ProblemDetails` and
`sbi_core::InvalidParam` already carried these fields; nothing in sbi-core's `problem_response`
could fill them. This ADR is step 1: sbi-core only, no NF changes (ADR-0572 item 4 is the SBI-03
line of the release plan).

**Decision:**

- `sbi_core::http2::problem_response` gets a five-argument overload that takes a
  `std::vector<sbi_core::InvalidParam>`. The four-argument overload now forwards to it with an
  empty list, and its body is byte for byte what it was. An empty list leaves the attribute out:
  `minItems: 1` forbids an empty array. `sbi_core::make_problem_details` gets the same overload.
  Every existing call site compiles unchanged.
- Four builders in `problem_details.hpp`, one per encoding of `param`:
  `invalid_body_ie(json_pointer, reason)`, `invalid_header(name, reason)`,
  `invalid_query_param(name, reason)` and `invalid_path_variable(name, reason)`.
  `invalid_body_ie` takes an `nlohmann::json::json_pointer`, so RFC 6901 escaping (`~0`, `~1`) is
  the library's, not each caller's. `reason` is optional, as in the YAML.
- `sbi_core::table_5272_note1_applies(cause)` is true for exactly the eight NOTE 1 causes above.
  Step 2 PRs and tests use it to find the call sites that must carry invalidParams.
- Tests: `SbiInvalidParams.*` in `tests/conformance/test_sbi_invalid_params.cpp` check the JSON
  field names against the YAML, the four `param` encodings, a round trip through
  `ProblemDetails`, the NOTE 1 cause set, and that a body without invalid params is unchanged.

**Helpers changed and not changed.**

- Changed: `problem_response` (new overload), `make_problem_details` (new overload).
- Not changed, `parse_json_body` and `parse_multipart_json_body`. They answer
  `MANDATORY_IE_MISSING` / `MANDATORY_IE_INCORRECT` from a `nlohmann::json` exception thrown in a
  generated `from_json`. That exception does not carry the JSON Pointer of the failing member:
  `out_of_range.403` names only the key (`key 'dnn' not found`), not where it is nested, and a
  `type_error` names no member at all. Writing `"/dnn"` from the key name would be wrong for a
  nested member, so these helpers still send no invalidParams. This is a remaining NOTE 1 gap,
  disclosed below.
- Not changed, `apply_json_patch_body` and `parse_json_patch_body`: their 400s are
  `INVALID_MSG_FORMAT`, which NOTE 1 does not cover.
- Not changed, the Authorizer (`authorizer.cpp`). Its 401 `ACCESS_TOKEN_CLAIM_MISSING` already
  writes the missing claims as invalidParams (TS 29.500 clause 6.7.3 text), with its own JSON
  builder; moving it to the new overload would change no output, so it stays.
- Not changed, the router's duplicate-member 400 in `http2_server.cpp`: `INVALID_MSG_FORMAT`.

**Verified (2026-11-21, master machine, a fresh worktree build at this commit, Debug).** The
default target of the whole tree (`cmake --build build`, 3024 of 3024 steps: every NF binary under
`nfs/`, `bss/`, the libraries and the test binaries) builds with 0 failed steps, so every existing
`problem_response` / `make_problem_details` call site compiles unchanged. `conformance_tests` runs
617 tests, all passing, the 5 new `SbiInvalidParams.*` and the existing `SbiJsonBody.*` /
`SbiJsonPatch.*` included. `test_li_amf_location`, `test_li_ngap_coexistence`,
`test_li_x1_gating`, `test_tmf640_dtos` and `test_tmf640_service` pass. `integration_tests` and
`nwdaf_mtlf_integration_tests` were not run (they need the lab certificates); not a full `ctest`.
CI result to be added.

**Disclosed.**
- No NF sends invalidParams after this ADR. Today 130 call sites in `nfs/` name one of the NOTE 1
  causes as a literal, and many already state the parameter in `detail` (e.g. the UDR's "Required
  query parameter ue-id is missing"). They move to the new overload in step 2 onward, one NF per
  PR.
- The 422 `parse_json_body` / `parse_multipart_json_body` call sites in `nfs/` and `bss/` keep
  answering `MANDATORY_IE_MISSING` / `MANDATORY_IE_INCORRECT` without invalidParams, as above.
  Closing that needs the failing member's path from the generated `from_json`; deferred for future
  implementation (see the rejected alternatives).
- `MISSING_PARAMETER` (403) is in the NOTE 1 set, but no NF here answers it today.

**Rejected alternatives.**
- *A default argument on the existing `problem_response`.* It would change the signature every
  NF links against; an overload keeps the four-argument symbol and its output as they are.
- *Fill `"/<key>"` from the `out_of_range` message in `parse_json_body`.* The message has no
  nesting, so a missing `/snssai/sst` would be reported as `/sst`. A wrong JSON Pointer is worse
  than none.
- *Turn on `JSON_DIAGNOSTICS` so nlohmann exceptions carry the JSON Pointer.* It is an ABI switch
  that every translation unit in the tree must agree on, and it adds a parent pointer to every
  `nlohmann::json` value. That is a tree-wide change, not an sbi-core one; it can be decided on its
  own, with a measurement.
- *Have `tools/sbi-codegen` emit path tracking in the generated `from_json`.* This is the likely
  route to the parse-helper gap, but it is a codegen subsystem change of its own.
- *Refuse a NOTE 1 cause without invalidParams in `problem_response` (assert or log).* Today it
  would fire on every existing NF call site, before step 2 has moved any of them.
