## ADR-0598: setup-asn1c.sh rebuilds a cached asn1c whose compiled-in skeleton path is not this checkout's

**Date:** 2026-11-20. **Status:** Accepted (user, 2026-11-20: "yes do the setup-asn1c hardening PR first").

**Context.** On self-hosted runners, `build-tools/` is a symlink to the persistent
`~/.cache/5gc-ci/build-tools`, so asn1c is built once per machine (`ci.yml`, ADR-0363).
`scripts/setup-asn1c.sh` reused any existing `build-tools/asn1c/bin/asn1c`. asn1c compiles its
skeleton directory (`<prefix>/share/asn1c`) into the binary as an absolute path, and the cached binary
on `MASTERMIND-007` (`5gc-lab-host`) had been built while the repository still had its former name. Its
skeleton path pointed into `_work/AI-Boosted-5G-Advanced-Core-and-Charging-R19/...`.

On 2026-11-20 the re-run of `main` CI (run 44668466088, build job on `5gc-lab-host`) failed on all three
attempts. asn1c warned `Cannot find standard modules in .../AI-Boosted-.../share/asn1c/standard-modules`,
copied no skeletons, and every `li_gen` C file then failed with `asn_application.h: No such file or
directory`. The same commit had built on that host at 03:36 UTC the same day, so the old directory was
removed in between. It is not known what removed it.

**Decision:**
- `setup-asn1c.sh` reuses a built asn1c only if (a) the first `.../share/asn1c` path embedded in the
  binary equals this checkout's `${ASN1C_PREFIX}/share/asn1c`, and (b) that directory holds
  `asn_application.h`. Otherwise it prints the path it found, removes `${ASN1C_PREFIX}` and rebuilds.
  The rebuild takes about 12 s on the master machine with the tarball already cached.
- The path is read with `grep -a`, so no `strings`/binutils dependency is added.
- A healthy cache behaves exactly as before.

**Verified.** Local, 2026-11-20, on `mastermindAI`:
- In this checkout (cache built here) the script prints "already built" and exits 0.
- In a scratch copy (`scripts/` plus a copy of `build-tools/asn1c`, so the embedded path pointed at the
  original checkout), it detected the mismatch, rebuilt (11.6 s), and the new binary embedded the scratch
  copy's own path. A second run then reused it.
- Not exercised on a runner yet. The first CI run on `5gc-lab-host` after merge is the real test, and
  should log the "rebuilding (ADR-0598)" line once.

**Disclosed.**
- The check reads the first matching string in the binary. That matches asn1c 0.9.29, where the data
  directory is the only such absolute path, but it is a heuristic over the binary, not an asn1c API.
  If a future asn1c embeds another `.../share/asn1c` string first, the worst case is a needless rebuild
  on each run, not a broken build.
- A checkout moved to another directory also triggers a one-time rebuild. That is correct, since the old
  binary points at the old path.

**Rejected alternatives.**
- *A stamp file recording the install prefix.* Rejected: every existing cache lacks the stamp and would
  rebuild once with no evidence of a problem. A stamp also records intent, not what the binary actually
  reads.
- *A functional probe (run asn1c on a tiny module and check that the skeletons are copied).* Rejected
  for now: more moving parts (a temp module, output parsing) for the same signal.
- *Fix only the runner by hand (`rm -rf ~/.cache/5gc-ci/build-tools/asn1c`).* Still the immediate
  remedy, but it fixes one machine once. The script fix covers any runner, now or later.
