## ADR-0549: AAnF SUPI path encoding and atomic AKMA context replace (SEC-15)

**Date:** 2026-11-01. **Status:** Closed (work pushed; last citing commit 69d51a54 on origin/main, 2026-11-10). Proposed.
**Context.** REVIEW-5ae0ef1 SEC-15 (Low) found two defects in the AAnF:

- **SUPI used unvalidated in UDM URLs.** `Naanf_AKMA_AnchorKey_Register` accepted the SUPI as given and pasted it into the `nudm-ee` and `nudm-sdm` request paths (`nfs/aanf/src/main.cpp`). A SUPI containing `/`, `?` or `#` changed the path the AAnF called.
- **Racy replace in `AkmaStore::put`.** `put` read the SUPI's old A-KID outside the MULTI. Two registrations for one SUPI could each delete only the A-KID they had read, leaving an orphaned A-KID record that still resolved to an old KAKMA. TS 33.535 §6.1 NOTE 1a says the new context replaces the old one. `remove_supi` had the same read-then-delete window.

The review hinted at a format check (`imsi-<digits>` / `nai-…`). That check cannot be the fix: TS 29.571 `Supi` is `^(imsi-[0-9]{5,15}|nai-.+|gci-.+|gli-.+|.+)$`, and its last branch admits any non-empty string. Following the user's 2026-10-24 decision to apply 3GPP strictly (recorded in the SEC-06/CHG-01 review answers), the AAnF may not reject what the spec pattern accepts. The URL problem therefore has to be solved by encoding, not by rejecting values.

**Decision:**

1. **The SUPI check matches the spec pattern, no more.** `supi_conforms()` rejects only what `.+` cannot match: an empty value, or one containing a line terminator (`\r`, `\n`, U+2028, U+2029). A failure returns 400 `MANDATORY_IE_INCORRECT`, "supi does not match the TS 29.571 Supi pattern".
2. **The SUPI is percent-encoded as a path segment** wherever the AAnF puts it into a UDM URL. The new header `libs/sbi-core/include/sbi_core/uri.hpp` provides `sbi_core::uri::encode_path_segment`, which follows RFC 3986 §3.3 `pchar` (TS 29.501 §5.2.3). It keeps unreserved characters, sub-delims, `:` and `@`, and encodes every other byte as `%XX` in uppercase hex.
3. **The sbi-core server decodes path parameters after splitting on `/`.** `Http2Server` now passes each `{param}` through `percent_decode(segment, false)` once the path has been split, with `+` kept literal as RFC 3986 requires for paths. An encoded `%2F` therefore stays inside its one segment and reaches the handler as `/`.
4. **`AkmaStore::put` and `remove_supi` each run as one Valkey Lua `EVAL`.**
   - `put` reads the old A-KID, deletes it if it differs, and writes both keys in one script.
   - If the A-KID record being written belonged to another SUPI, `put` also drops that SUPI's index entry, but only when the entry still names this A-KID. This leaves no index entry naming a record it does not own.
   - `remove_supi` reads the index and deletes both keys in one script.
   - Every key either script touches is under `aanf:`, the prefix the AAnF's Valkey ACL user is limited to (ADR-0514). That ACL (`~aanf:* +@all -@admin -@dangerous`) permits `EVAL`.

**Tests.**

- `Http2RoutePrecedence.AnEncodedPathParameterArrivesDecodedAsOneSegment` encodes `nai-a/b?c#d%e+f g@realm`, checks the exact encoding, and checks that the route handler receives the original value.
- `AanfAuthorization.ReRegisteringASupiReplacesItsContextAtomically` runs against the real NRF, AAnF and Valkey:
  - 8 concurrent registrations for one SUPI leave exactly one `aanf:akid:*` record, and the index names it.
  - A sequential re-registration drops the old A-KID.
  - An A-KID that moves to another SUPI removes the first SUPI's index entry.
  - A newline SUPI and an empty SUPI are rejected with 400.
  - A `nai-` SUPI containing `/` registers and removes correctly.

**Disclosed.**

- **The SUPI check is deliberately permissive.** The AAnF accepts any SUPI the TS 29.571 pattern accepts. Path safety comes from encoding (point 2), not from rejecting values.
- **Point 3 changes behaviour for every NF built on sbi-core**, not only the AAnF. Path parameters used to reach handlers raw (still encoded); now they arrive decoded. Before this change no in-tree client encoded path parameters, so for the identifiers in use today the raw and decoded values are equal. A client that did send `%XX` in a path would have been matched against the encoded form before, which was non-conformant.
- **Other NFs' outbound URLs are not changed by this ADR.** Encoding SUPIs and other identifiers in other NFs' outbound URLs is deferred for future implementation, NF by NF, using the same `encode_path_segment`.
- **The Lua scripts build key names inside the script** (`'aanf:akid:' .. old`) and do not declare them in `KEYS`. That is correct on a single Valkey node, which is what the lab and the Helm chart deploy. It would not be safe under Valkey Cluster, where every key must be declared and hash to one slot. Cluster mode is deferred for future implementation.

**Rejected alternatives.**

- **A strict format check** (`imsi-[0-9]{5,15}` or `nai-` followed by a fixed character set). It is non-conformant: it rejects SUPIs the TS 29.571 pattern allows.
- **Rejecting `/`, `?`, `#` or `..` in the SUPI.** This is also stricter than the spec pattern, and it would still leave the encoding of other characters undefined.
- **`WATCH`/`MULTI` with a retry loop.** It is correct, but needs a client-side retry policy and a bound on retries under contention. A single script has no retry path and no window.
- **Decoding the whole path before splitting it.** An encoded `%2F` would become a path separator, which is the bug this ADR fixes.
