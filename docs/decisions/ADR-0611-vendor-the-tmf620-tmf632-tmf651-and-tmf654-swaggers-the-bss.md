## ADR-0611: Vendor the TMF620, TMF632, TMF651 and TMF654 swaggers the BSS services cite (BSS SBI-03 PR 0)

**Date:** 2026-11-21. **Status:** Proposed. The user approved the scope on 2026-11-21: specs and
docs only, no code changes.

**Context.** The BSS part of SBI-03 (ADR-0572) will check what the BSS services answer on errors.
That check needs the TM Forum swagger each service implements, in the repository, so that a review
can cite a line. Only `specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json` is vendored
today (ADR-0558, ADR-0568). Four services name a swagger in their header comment and say it was
"fetched and parsed directly, not vendored":

| Service | Header that cites it | Repository | File |
|---|---|---|---|
| `bss/product-catalog` | `bss/product-catalog/src/main.cpp`, `libs/bss-sid/include/bss_sid/product.hpp` | `tmforum-apis/TMF620_ProductCatalog` | `TMF620-ProductCatalog-v4.1.0.swagger.json` |
| `bss/subscriber-management` | `bss/subscriber-management/src/main.cpp`, `libs/bss-sid/include/bss_sid/party.hpp` | `tmforum-apis/TMF632_PartyManagement` | `TMF632-Party-v4.0.0.swagger.json` |
| `bss/roaming-interconnect` | `bss/roaming-interconnect/src/main.cpp`, `libs/bss-sid/include/bss_sid/agreement.hpp` | `tmforum-apis/TMF651_AgreementManagement` | `TMF651-Agreement-v4.0.0.swagger.json` |
| `bss/balance-management` | `bss/balance-management/src/main.cpp`, `libs/bss-sid/include/bss_sid/balance.hpp` | `tmforum-apis/TMF654_PrepayBalanceManagement` | `TMF654-PrepayBalance-v4.0.0.swagger.json` |

The headers give no commit. TMF678 (`libs/bss-sid/include/bss_sid/rating.hpp`) is cited the same
way but is not in this PR's approved scope.

**Decision:**

- The four files are vendored in `specs/tmforum/` byte for byte, under the file names the headers
  cite. Each was fetched on 2026-11-21 from `raw.githubusercontent.com` at the head of `main` of
  its repository, and that commit is pinned:

  | File | Commit (date) | SHA-256 |
  |---|---|---|
  | `TMF620-ProductCatalog-v4.1.0.swagger.json` | `24e69f3026f2b514baa27d7206540fc7b55bcb45` (2026-08-07) | `10db69222fcdbeb7e99b423af3e83953bcacc832278af944016cdc05a5ed60cc` |
  | `TMF632-Party-v4.0.0.swagger.json` | `11c6a0ecf2b9a7007f43e86601b37e45d9f39b1e` (2025-11-07) | `866d72c02e3dc1e4baffb8db733b99fd2554ddc822bee6a020287037c33240af` |
  | `TMF651-Agreement-v4.0.0.swagger.json` | `008dae3a3f929c97a34f767d051981aadb285b6c` (2026-08-07) | `b55f1514d566c7dee1b08b2a81a3b6e2292f9da079041bc3a3ca12eb2a0fb88a` |
  | `TMF654-PrepayBalance-v4.0.0.swagger.json` | `562e655d14d796b8380b4e244a5b601f69d60b64` (2026-08-07) | `f397aea173cffeedb0bad4d7dd89ea3e469b881ed6e350828325c3ffa2751920` |

- `specs/tmforum/SOURCES.md` gets one row per file, in the TMF640 row's format, and records the
  commit that last changed each file. `specs/README.md`'s `tmforum/` row names the new files.
- All four repositories are Apache-2.0 (GitHub reports `Apache-2.0`). Each repository's `LICENSE`
  is byte-identical to the `specs/tmforum/LICENSE` already copied for TMF640 (SHA-256
  `65ac57b1de61ae34baed5b654568d6799f8e5300ab75cf363d4a977b146fa4df`), so that one file covers
  all five. Apache-2.0 is OSI-approved and allows redistribution with the licence kept.
- Nothing builds from the four files yet. `libs/tmf-generated` still generates only TMF640, and
  the four services keep their hand-written JSON. Moving them onto generated DTOs stays deferred
  for future implementation (ADR-0568 *Disclosed*).

**What the five files say about errors.** Extracted on 2026-11-21 with a script kept outside
`specs/` (`python3 -I`, the files read as JSON data only).

The `Error` definition is identical in all five files (compared as parsed JSON):

| File | `Error` lines |
|---|---|
| `TMF640-ServiceActivation-v4.0.0.swagger.json` | 3405-3448 |
| `TMF620-ProductCatalog-v4.1.0.swagger.json` | 9165-9208 |
| `TMF632-Party-v4.0.0.swagger.json` | 3919-3962 |
| `TMF651-Agreement-v4.0.0.swagger.json` | 3351-3394 |
| `TMF654-PrepayBalance-v4.0.0.swagger.json` | 5972-6015 |

`type: object`, `required: [code, reason]`. Properties: `code` (string), `reason` (string),
`message` (string), `status` (string, "HTTP Error code extension"), `referenceError` (string,
`format: uri`), `@baseType` (string), `@schemaLocation` (string, `format: uri`), `@type` (string).
`status` is a string, not an integer.

Every 4xx/5xx response of every operation in the five files has the schema
`$ref: #/definitions/Error`. No other error schema occurs. The statuses declared are 400, 401, 403, 404,
405, 409 and 500, with two exceptions in every file:

- A create (`POST` on a collection) declares no 404: TMF640 `POST /service`; TMF620 `POST
  /catalog`, `/category`, `/productOffering`, `/productOfferingPrice`, `/productSpecification`,
  `/importJob`, `/exportJob`; TMF632 `POST /individual`, `/organization`; TMF651 `POST
  /agreement`, `/agreementSpecification`; TMF654 `POST /topupBalance`, `/adjustBalance`,
  `/transferBalance`, `/reserveBalance`.
- `DELETE /hub/{id}` declares no 409.

Operations per file: TMF640 17, TMF620 56, TMF632 20, TMF651 20, TMF654 40. No file declares 422,
501 or 503.

**Rejected alternatives.**

- **Pin the commit that last changed each file** (TMF620 `4a76d6bc`, TMF632 `e4642b97`, TMF651
  `17167ad9`, TMF654 `5be484f9`). The files were fetched at the `main` head, and the TMF640 row pins
  the head it was fetched from too. The last-change commits are recorded in `SOURCES.md` instead.
- **Vendor the `TMFnnn_<Name>_API_v4.x.x_swagger.json` copies as well.** At the same commits their
  `paths` and `definitions` are identical to the vendored files (compared 2026-11-21), and the
  headers do not cite them. The TMF640 copy was left out for the same reason.
- **Vendor TMF678 in the same PR.** Outside the approved scope. It can follow as its own PR.
- **Keep fetching live.** A live fetch can change under a review, and a header without a commit
  cannot be re-checked later.

**Disclosed.**

- The headers cite a repository, a file and a version but no commit. The vendored copy is today's
  `main` head. The file last changed in 2019-2021 in each repository, so it is very likely what the
  headers' authors read, but that cannot be proven.
- `basePath` differs in style between files (TMF620, TMF632 and TMF651 end in `/`, TMF640 and
  TMF654 do not). That is as published and is not changed here.
- This ADR changes no code and no handler behaviour. Whether each BSS service answers the error
  statuses and the `Error` body above is the subject of the later SBI-03 BSS PRs.

**Verified (2026-11-21, this machine).** For each file, the git blob SHA-1 of the copy equals the
one `GET /repos/tmforum-apis/<repo>/contents/<file>?ref=<commit>` reports, and the size matches.
`sha256sum specs/tmforum/*.json` gives the hashes above. `python3 scripts/docs/check_adr_index.py`
passes.
