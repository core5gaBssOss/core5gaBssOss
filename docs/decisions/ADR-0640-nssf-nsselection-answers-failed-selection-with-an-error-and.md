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
  The store is keyed by AMF `nfId` and holds each AMF's raw `NssaiAvailabilityInfo`
  (`nfs/nssf/src/main.cpp` line 561), so "listed as supported in that TA" has to be defined over
  all AMFs' entries. Proposed: an S-NSSAI is supported in a TA if any stored entry's
  `supportedNssaiAvailabilityData` for that `tai` lists it. A TA that no AMF has reported is
  treated as unknown and falls back to the catalog, so a lab without NSSAIAvailability traffic
  keeps today's answers. **Open:** confirm both rules against TS 29.531 / TS 23.501 5.15 before
  coding.
- **Procedure outputs.** `configuredNssai` for registration gets filled from the catalog
  (`ConfiguredSnssai` requires only `configuredSnssai`). `nsiInformation` cannot come from the
  catalog: `NsiInformation` requires `nrfId` (the slice's NRF URI), and the catalog is two
  hard-coded S-NSSAIs (`seed_catalog()`, line 103) with no NRF data. Filling it needs a
  per-S-NSSAI NRF entry in the NSSF's configuration (guardrail 8: configuration over code); until
  that config exists `nsiInformation` stays out of scope with the other outputs below. `targetAmfSet`/`candidateAmfList` and
  `mappingOfNssai` stay out of scope (no AMF-set or roaming mapping data source yet) and remain a
  disclosed simplification in the file's top comment.
- Sequencing: open PR #79 (ADR-0629) changes the same NSSelectionGet handler (its 400 carries
  `invalidParams`). The implementation PR goes after #79 merges.
- Tests: one negative test per procedure for the error answer, one test where an AMF's
  NSSAIAvailabilityPut withdraws a slice in a TA and NSSelection then rejects it in that TA.

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
