# specs/

The specification material this repository builds from, and the documents it cites.

## In the repository: build inputs only

| Path | What | Used by | Copyright / licence |
|---|---|---|---|
| `5G_APIs-REL-19/` | 3GPP OpenAPI YAML, REL-19 branch of forge.3gpp.org/rep/all/5G_APIs (commit in `.source-commit`) | `sbi_generated` codegen, every NF's API | © 3GPP Organizational Partners (ARIB, ATIS, CCSA, ETSI, TSDSI, TTA, TTC), as stated in each file |
| `NGAP/ngap-17.9.asn` | TS 38.413 ASN.1, patched for asn1c (ADR-0031) | `ngap_generated` | © 3GPP Organizational Partners |
| `3gpp/33128-attachments/` | TS 33.128 ASN.1 and XSD attachments | `li_generated`, LI X1 validation | © 3GPP Organizational Partners |
| `3gpp/MANIFEST.tsv` | the exact 3GPP spec versions the code cites | `tools/specs/fetch_3gpp_specs.py --from-manifest` | project file |
| `etsi/` | ETSI forge schemas and ASN.1 (TS 102 232, 103 120, 103 221, 103 280) | `li_core`, `li_generated`, conformance tests | BSD-3-Clause, `etsi/LICENSE`; provenance in `etsi/SOURCES.md` |
| `MAC-AUSF.TXT` | the project's own implementation notes for the TS 33.501 SoR-MAC derivations | AUSF (reference) | project file, Apache-2.0 |

## Not in the repository: specification documents

The 3GPP and ETSI specification documents (PDF, Word and zip files, and full-text extracts of
them) are copyrighted by their publishers and are **not redistributed here** (ADR-0507). They
were removed from the whole git history on 2026-10-22. Source comments still cite them by the
path they are fetched to, so a citation such as `specs/TS_33_501.pdf` or
`specs/3gpp/TS_23.288_j70.txt` resolves once the document has been fetched. `.gitignore` keeps
fetched documents out of git.

**3GPP specs cited from `specs/3gpp/`** (zip, Word file and text extract, at the versions in
`3gpp/MANIFEST.tsv`), from the 3GPP archive (needs LibreOffice for the text extract):

```bash
python3 tools/specs/fetch_3gpp_specs.py --from-manifest
```

**ETSI deliverables cited from `specs/etsi/`**: `python3 tools/specs/fetch_etsi_specs.py`
(see `etsi/SOURCES.md`).

**Reference PDFs cited from `specs/`.** Download each from www.3gpp.org (3GPP TS) or
www.etsi.org/deliver (ETSI TS, the same text republished by ETSI) and save it under the name
given; the version is the one the code was written against.

| Save as | Document |
|---|---|
| `specs/TS_28_532.pdf` | 3GPP TS 28.532 V19.3.0 (2025-12) |
| `specs/TS_28_541.pdf` | 3GPP TS 28.541 V19.6.0 (2025-12) |
| `specs/TS_28_552.pdf` | ETSI TS 128 552 V19.8.0 (2026-08) |
| `specs/TS_28_554.pdf` | ETSI TS 128 554 V19.7.0 (2026-04) |
| `specs/TS_28_622.pdf` | 3GPP TS 28.622 V19.6.0 (2025-12) |
| `specs/TS_29_500.pdf` | ETSI TS 129 500 V19.7.0 (2026-08) |
| `specs/TS_32_240.pdf` | 3GPP TS 32.240 V19.4.0 (2025-09) |
| `specs/TS_32_290.pdf` | 3GPP TS 32.290 V19.4.0 (2025-09) |
| `specs/TS_32_291.pdf` | 3GPP TS 32.291 V19.4.0 (2025-09) |
| `specs/TS_32_298.pdf` | ETSI TS 132 298 V18.8.0 (2025-04) |
| `specs/TS_32-299.pdf` | ETSI TS 132 299 V19.0.0 (2025-10) |
| `specs/TS_32_401.pdf` | 3GPP TS 32.401 V19.0.0 (2025-03) |
| `specs/TS_33_501.pdf` | ETSI TS 133 501 V19.6.0 (2026-04) |
| `specs/TS_33_503.pdf` | ETSI TS 133 503 V19.3.0 (2026-07) |
| `specs/ts_129002v190000p.pdf` | ETSI TS 129 002 V19.0.0 (2026-01) |
| `specs/ts_129078v190000p.pdf` | ETSI TS 129 078 V19.0.0 (2026-01) |
| `specs/ts_129219v190000p.pdf` | ETSI TS 129 219 V19.0.0 (2025-10) |
| `specs/PFCP/29244-e30.pdf` | 3GPP TS 29.244 V14.3.0 (2018-03) |

Versions above were read from each document's own title page before removal.
