## ADR-0507: Specification documents are not redistributed: removed from history, fetched on demand; NOTICE and REUSE licensing

**Date:** 2026-10-22. **Status:** Accepted.

**Context.** `specs/` held 322 MB of third-party documents committed over the project's life:
- 3GPP specification zips and Word files, and full-text extracts of them (`specs/3gpp/`, 52 specs);
- 3GPP and ETSI PDFs supplied by hand (`specs/TS_*.pdf`, `specs/ts_1290*.pdf`,
  `specs/PFCP/29244-e30.pdf`);
- two documents that are not specifications at all (`specs/GPT6-Model-Recommendation.pdf`,
  `specs/5G-Advanced_Protocols_and_Node_Capabilities_R19_A1_print.pdf`).

Their publishers hold the copyright, and this repository redistributed them to anyone who cloned
it. They made up most of the 256 MB clone. The build reads none of them: comments cite them as the
source a value was read from. ETSI deliverables were already kept out (`specs/etsi/*.pdf`,
gitignored); the rest were not. The professional-setup review (2026-10-22) put this first. The
user decided to remove the documents from history, not only from the current tree.

**Decision:**
- **Removed from the whole history (2026-10-22)** with git-filter-repo 2.47.0, on every branch:
  `specs/**/*.pdf`, `*.docx`, `*.doc`, `*.zip` and `specs/3gpp/TS_*.txt`. Every branch's tip tree
  was checked to differ from its pre-rewrite tip by exactly those files, with the same commit count.
  All 17 branches were force-pushed. The pack went from 256 MB to 22 MB.
- **Kept, because the build reads them:** the 3GPP OpenAPI YAML, NGAP ASN.1, TS 33.128 ASN.1/XSD,
  the ETSI forge schemas (BSD-3-Clause) and `specs/3gpp/MANIFEST.tsv`. Also kept is
  `specs/MAC-AUSF.TXT`, the project's own notes, not a third-party document.
- **Fetched on demand:** `tools/specs/fetch_3gpp_specs.py --from-manifest` fetches every 3GPP spec
  at the version in `MANIFEST.tsv`, the versions the code cites. Before this it took the newest
  Rel-19 version, which can differ. `specs/README.md` lists the hand-supplied PDFs with the version
  read from each title page, saved under the path the code cites. `.gitignore` keeps fetched
  documents out.
- **Licensing files:**
  - `NOTICE` (Apache-2.0 practice): the project copyright and the third-party material, with a
    note that implementing 3GPP specifications may need licences beyond this project's.
  - `REUSE.toml` and `LICENSES/`: Apache-2.0 for the project's files, BSD-3-Clause for
    `specs/etsi/`, and `LicenseRef-3GPP-Copyright` for the vendored 3GPP material. That reference
    quotes the copyright statement the 3GPP files carry and states no licence they do not state.
- `docs/history/2026-10-22-spec-document-purge-commit-map.txt`: each old commit id mapped to its new
  one. ADRs and other documents cite commit ids from before the rewrite, and those ids no longer
  exist on GitHub.

**Rejected alternatives.**
- *Remove from the current tree only.* The documents would stay in every clone and in GitHub's
  history.
- *Git LFS or a release asset for the documents.* Either one is still redistribution.
- *SPDX headers in every source file.* A header in 683+ files would touch every open PR for no gain:
  `REUSE.toml` covers the same files, and `reuse lint` accepts it.
- *Drop the 3GPP YAML and ASN.1 too, fetching them at build time.* CLAUDE.md requires the YAML
  offline and generates every API from it. Open implementations vendor the same files. They stay,
  attributed.

**Verified (2026-10-22).**
- 17/17 branch tip trees equal the pre-rewrite trees minus the purged paths, and no document
  remains in any branch's history.
- Open PRs #21, #22, #24, #26, #27 and #28 kept their file counts and line counts.
- #23, #25 and #29 had conflicted with `main` since PR #9 merged. They were replayed commit by
  commit, and the ADR index check passes on each.
- `fetch_3gpp_specs.py --from-manifest` fetched TS 23.038 j00: the zip is byte-identical to the
  removed copy, and the text extract is identical.
- `reuse lint` (reuse 5.0.2) on `main` plus these files: compliant with REUSE 3.3, 2332/2332 files
  with copyright and licence information.

**Disclosed.**
- **GitHub still holds the old commits** through the hidden refs of earlier pull requests
  (`refs/pull/*/head`), which only GitHub Support can remove, and in cached views. **One fork**
  exists and keeps its own copy.
- **Old commit ids are gone from GitHub.** Commit ids written into ADRs and other documents before
  2026-10-22 refer to the old history; the commit map translates them.
- **Local clones made before the rewrite still contain the documents** until they are re-cloned
  or garbage-collected.
- **Not added:** a CI check that refuses a committed specification document. `.gitignore` is the
  only guard.
