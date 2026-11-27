## ADR-0640: NSSF NSSelection answers failed selection with an error and consults the NSSAI availability store

**Date:** 2026-11-27. **Status:** Proposed.

**Context.** An external reviewer (Pinaki Mukherjee, who works on NSSF/NRF slicing products) read
`nfs/nssf/src/main.cpp` on main and reported two gaps in Nnssf_NSSelection (ADR-0183). Both were
checked against the code at `origin/main` 9022f3a6 and hold:

1. **A failed selection still answers 200, without the procedure's own outputs.** The
   `GET /nnssf-nsselection/v2/network-slice-information` handler (lines 411-463) always returns
   `Response::json(200, ...)`. When no requested S-NSSAI is in the catalog, `decide_slice_selection`
   (lines 138-169) leaves `allowedNssaiList` absent and lists everything in `rejectedNssaiInPlmn`,
   and that still goes out as 200. The reviewer cites TS 29.531 §5.2.2.2.2, which describes the
   selection outcome per procedure. The PDU-session case is the clearest one: one S-NSSAI is asked
   about, it is not supported, and the answer is still 200 with an empty selection. The response
   also never carries what the procedure asks for. Registration and UE configuration update can
   return `configuredNssai`, `targetAmfSet`/`candidateAmfList`, `mappingOfNssai` and
   `nsiInformation`. PDU session can return `nsiInformation`. None of these is filled. That was
   disclosed as simplification 1 in the file's top comment, but the 200-on-failure was not.
2. **Selection ignores the availability store.** `NssaiAvailabilityStore availability_store`
   (line 360) holds the per-TA S-NSSAI support that AMFs PUT/PATCH through Nnssf_NSSAIAvailability.
   The NSSelection route captures only `nssai_catalog`, so the `tai` query parameter is never
   read and a slice an AMF withdrew for a TA is still allowed there.

The reviewer says these are examples and that more such findings exist. Further review is
requested in reply, and further findings join this ADR or get their own ADRs.

**Decision:** Proposed, to be implemented in its own PR under this ADR:

- **Failure is an error, not a 200.** If nothing can be allowed for the request (no S-NSSAI from
  the request passes), NSSelectionGet answers with an error `ProblemDetails` from the YAML's
  response set (`403` in `TS29531_Nnssf_NSSelection.yaml`), not 200. A partial result
  (some allowed, some rejected) stays a 200 with `rejectedNssaiInPlmn`/`rejectedNssaiInTa`. The
  exact status and `cause` per procedure must be read from TS 29.531 §5.2.2.2 and its
  application-error table before coding. **Open:** the TS 29.531 prose (`29531-j??.docx`) is not
  in `specs/3gpp/`; it has to be added first (guardrail: spec is the source of truth).
- **Selection reads the availability store.** When the `tai` query parameter is present, a
  candidate S-NSSAI is allowed only if the catalog has it *and* the availability store lists it as
  supported in that TA. S-NSSAIs that fail only on the TA go to `rejectedNssaiInTa`, not
  `rejectedNssaiInPlmn`. Without `tai`, today's catalog-only behaviour stays, and that is disclosed.
- **Procedure outputs.** `nsiInformation` for the PDU-session procedure and `configuredNssai` for
  registration get filled from the catalog. `targetAmfSet`/`candidateAmfList` and
  `mappingOfNssai` stay out of scope (no AMF-set or roaming mapping data source yet) and remain a
  disclosed simplification in the file's top comment.
- Tests: one negative test per procedure for the error answer, one test where an AMF's
  NSSAIAvailabilityPut withdraws a slice in a TA and NSSelection then rejects it in that TA.

**Implementation (2026-11-27).** TS 29.531 j70 (`specs/3gpp/29531-j70.docx`) was added and read.
Clauses 5.2.2.2.2-5.2.2.2.5 step 2b settle the open point: no slice found -> `403` with
application error `SNSSAI_NOT_SUPPORTED` (Table 6.1.7.3-1). The decision moved out of main.cpp
into `nfs/nssf/src/selection.cpp` (pure; conformance tests link it):
- Each candidate is allowed, or goes to `rejectedNssaiInPlmn` (not in the catalog), or to
  `rejectedNssaiInTa` (an NSSAIAvailability record covers the request's `tai` through `tai` or
  `taiList`, and none of its `supportedSnssaiList` ExtSnssai entries match by sst + sd,
  `wildcardSd` or `sdRanges`).
- Registration: when none of the Requested NSSAI is permitted, the subscribed S-NSSAIs with
  `defaultIndication` that pass both checks are allowed (Table 6.1.6.2.2-1 allowedNssaiList, third
  condition). `configuredNssai` is the subscribed S-NSSAIs in the catalog (or the catalog), and it
  is sent when there is no Requested NSSAI or one of them is not valid in the PLMN. UE
  configuration update always sends it.
- PDU session: the 200 carries only `nsiInformation`, with `nrfId` = `{nrf_base_url}/nnrf-disc/v1`
  (Table 6.1.6.2.7-1: the NFDiscovery API URI). No `allowedNssaiList` is sent for it.
- 403 when nothing is allowed and there is no `configuredNssai`. A PDU session gets 403 when its
  S-NSSAI is not allowed.

Still not modeled, and disclosed in main.cpp's top comment:
- `targetAmfSet`/`candidateAmfList`. 5.2.2.2.2 step 2a requires one of them on a registration
  200, so that answer stays non-conformant.
- `taiRangeList`, `mappingOfNssai`, `nsagInfos`, `targetNssai`, NSSRG compatibility, and a
  per-slice NRF/`nsiId`.
- A TA no AMF has reported is decided by the catalog alone.

The repo's `TS29531_*.yaml` differ from the copies in the j70 zip. The checked-in REL-19 YAML stays
the codegen source. Every field used here is in both.

**Verified (2026-11-27).**
- `tests/conformance/test_nssf_selection.cpp`: 12 `NssfSelection.*` tests, all passing. They were
  linked against the CI build's `libsbi_generated.a`.
- Live: the new `nssf` binary against an `nrf` with real tokens, using the same five requests as
  the integration test. Results: TA-withdrawn SST 2 -> 403 `SNSSAI_NOT_SUPPORTED`; SST 1 -> 200
  `nsiInformation.nrfId`; unknown SST 3 -> 403; registration -> `rejectedNssaiInTa` [SST 2];
  default-subscribed fallback -> `configuredNssai`.
- `NssfAuthorization.NsSelectionConsultsNssaiAvailabilityAndAnswers403WhenNoSlice`
  (tests/integration) compiles but was not run locally. CI runs it, and it fails on the old code,
  which answered 200.

**Rejected alternatives.**
- *Keep 200 and rely on `rejectedNssaiInPlmn` alone.* That is what the code does now, and the
  reviewer flags it as non-conformant. A consumer AMF/SMF cannot tell "nothing selected" from a
  success without inspecting the body.
- *Replace the static catalog with the availability store.* The store holds what AMFs report per
  TA, not the network's supported NSSAI. Both are needed: the catalog for PLMN-level support and
  the store for TA-level support.
- *Answer 404 for an unsupported S-NSSAI.* 404 in TS 29.500 means the resource is not found.
  The network-slice-information resource exists, so the refusal is a 403 application error. To be
  confirmed against the TS 29.531 prose (see Open above).
