## ADR-0483: Build asn1c with `-fno-strict-aliasing` (GCC 15.2 miscompiles its NGAP codegen into a segfault)

**Date:** 2026-10-12. **Status:** Proposed.

**Context.**

ADR-0482 moved the lab host and all 31 NF Dockerfiles to Ubuntu 26.04 / GCC 15.2. Rebuilding the
Docker images (the "images not yet rebuilt/run" item that commit left open) made every one of the
32 `docker compose build`-able services fail identically at CMake configure time:
`libs/ngap-generated/CMakeLists.txt:64`, `asn1c failed generating the NGAP codec at configure
time:` with an **empty** error body. The empty body was CMake's `execute_process` swallowing the
real error -- the host kernel log showed the actual cause:

```
kernel: asn1c[94227]: segfault at 5664df396052 ip 00005664df38fc24 ... in asn1c[1cc24,...]
kernel: asn1c[117008]: segfault at 642f982a4052 ip 0000642f9829dc24 ... in asn1c[1cc24,...]
```

Two independent builds (the solo `nrf` build and, three minutes later, the parallel `ausf` build)
crashed at the **identical offset** inside the `asn1c` binary -- deterministic, not a race or an
OOM (confirmed no OOM-kill entries in the same window).

Isolated and root-caused directly on the host, with no Docker involved:

- The host's existing `build-tools/asn1c/bin/asn1c` (dated 2026-08-07, i.e. pre-crash, compiled
  back when the host ran Ubuntu 24.04 / GCC 13.3, restored from backup per ADR-0482's
  host-toolchain.md inventory) runs `asn1c -pdu=all ... specs/NGAP/ngap-17.9.asn` successfully,
  exit 0.
- Rebuilding the *exact same* asn1c 0.9.29 source with the host's *current* default GCC 15.2:
  segfault, exit 139.
- Rebuilding again with `CFLAGS="-g -O0"` (optimization disabled): no crash. This pointed at an
  optimization-triggered bug, not a fundamental GCC-15 incompatibility.
- `gdb` on a `-g`, default-optimization (`-O2`) GCC-15 build gave a real backtrace (CMake's
  `execute_process` had been swallowing it):
  ```
  Program received signal SIGSEGV, Segmentation fault.
  0x0000555555570c24 in asn1f_fix_constr_ext (arg=arg@entry=0x7fffffffd710) at asn1fix_constr.c:144
  144           TQ_ADD(cur_list, v, next);
  ```
  with `ext_list.tq_tail` holding a pointer into the binary's own `.text` segment -- consistent with
  the kernel's `error 7` (write to a present, read-only page): the tail-queue write landed on code,
  not data.

**Root cause.** `libasn1fix/asn1fix_constr.c`'s `asn1f_fix_constr_ext()` declares three tail-queue
locals -- `root_list`, `ext_list` (`TQ_HEAD(asn1p_expr_t)`), and `cur_list`
(`TQ_HEAD(asn1p_expr_t) *`) -- and reassigns `cur_list` between `&root_list` and `&ext_list`
through a `(void *)` cast as it splits a field list. `TQ_HEAD(type)`
(`libasn1parser/asn1p_list.h:7-11`) is a macro expanding to an **anonymous struct literal at every
use site** -- so `root_list`, `ext_list`, and the type `cur_list` points to are three structurally
identical but nominally distinct C types. Assigning between them through `(void *)` is a strict-
aliasing violation: real, confirmed undefined behavior in 25-year-old code, not a guess. GCC 15.2's
type-based alias analysis exploits this UB more aggressively than GCC 13.3's did, corrupting
`cur_list->tq_tail` by the time `TQ_ADD` dereferences it.

This is not this project's own Aligned PER patch (`scripts/patches/asn1c-aligned-per.patch`,
ADR-0031): that patch only touches asn1c's skeleton *runtime* sources (`per_support.c`,
`per_decoder.c`, etc. -- copied into each NF binary at build time), never the compiler's own
`libasn1fix/` source where this bug lives.

The host's own native build (ADR-0482's "0 compile errors, all 27 NF binaries") never exercised
this: it silently kept using the pre-crash GCC-13-built binary, because `scripts/setup-asn1c.sh` is
idempotent and skips rebuilding when `build-tools/asn1c/bin/asn1c` already exists. Every Docker
image build, by contrast, starts from an empty `build-tools/` and compiles asn1c fresh inside the
container -- with the container's GCC 15.2 -- so Docker was the first thing in this project to
actually exercise a GCC-15-compiled asn1c, and the first thing to hit this bug.

**Decision:**

Build asn1c's own compiler with `CFLAGS="-g -O2 -fno-strict-aliasing"` in `scripts/setup-asn1c.sh`
(explicit `-O2` kept to match the project's default optimization level; only the aliasing
assumption is disabled). `-fno-strict-aliasing` is the standard, widely-used mitigation for exactly
this class of bug -- the same flag the Linux kernel and CPython build with, for the same reason --
and it is a no-op on compilers that never exploited the UB in the first place, so it is correct on
every GCC version this project uses, not specific to 15.2. No Dockerfile changes are needed: NF code
keeps building with the project's default compiler everywhere; only this one flag changes how
asn1c's own `make` step runs.

**Verification:** rebuilt with the flag under the host's GCC 15.2, ran the identical
`asn1c -pdu=all ... specs/NGAP/ngap-17.9.asn` invocation three times (exit 0 each time, 2222 files
each time), and diffed the output against the existing known-good `build/generated/ngap_gen/`
(generated by the pre-crash GCC-13 binary): identical except for the generated header comment's
embedded invocation path (absolute path vs. a different output directory -- cosmetic, expected
given the two runs used different working directories) and `Makefile.am.libasncodec` (same cause).
Zero semantic differences in any of the 2222 generated `.c`/`.h` files.

**Rejected alternatives.**

- *Pin `gcc-13`/`g++-13` for asn1c's own build, leave the UB unfixed.* The first fix attempted in
  this session, before root-causing the actual bug. Works, but only by accident of GCC 13 not
  happening to exploit this particular UB -- it is not actually portable to whatever GCC version
  comes after 15, would need every Dockerfile's builder stage to additionally install `gcc-13
  g++-13` (two compilers in the toolchain, 31 Dockerfile edits), and leaves the real bug in place
  for the next compiler upgrade to rediscover. Superseded by this ADR once the actual root cause was
  found.
- *Vendor the pre-crash (2026-08-07, GCC-13-built) binary into Docker images instead of rebuilding
  from source per image.* Fast, but ties every image build to one host's build artifact -- directly
  against `scripts/setup-asn1c.sh`'s own header comment ("this script is how every dev machine and
  CI reproduces it, since committing a patched compiler's build output isn't appropriate for this
  repo"). Also does nothing for the next clean host restore, which would hit the identical crash the
  moment `setup-asn1c.sh` runs fresh.
- *Patch `asn1fix_constr.c` to use a named, shared struct type instead of three anonymous ones.*
  The most correct possible fix (removes the UB at the source instead of papering over it with a
  compiler flag), but means patching asn1c's *compiler* source in addition to the existing skeleton-
  runtime patch (ADR-0031) -- a second, separately-maintained patch against upstream for a 25-year-
  old codebase with no upstream activity. Not pursued: `-fno-strict-aliasing` fixes the symptom
  project-wide with one line and no new patch file to maintain; revisit only if a *different*
  strict-aliasing-dependent bug surfaces elsewhere in asn1c that this flag doesn't cover.
- *Downgrade the whole project to GCC 13.* Rejected outright -- GCC 15.2 is the ADR-0482 host/image
  default for all NF code for unrelated reasons (26.04 default compiler, stricter standards
  conformance); only asn1c's own, narrowly-scoped compiler build was ever affected by this bug.
