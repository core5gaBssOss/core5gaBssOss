## ADR-0482: Lab host and NF images move to Ubuntu 26.04 with GCC 15; clang-tidy cost measured

**Date:** 2026-10-11. **Status:** Accepted (host + images, and lint option 2 -- all user-directed).

**Context.** The lab laptop (also the only CI runner, ADR-0363) crashed on 2026-10-11 while running
CI run 38057786320 -- `lint` had passed (4 h 18 min), `build` had compiled, and the job died in Test
with "self-hosted runner lost communication": the crash, not a code defect. The repo was restored
from a snapshot onto a fresh Ubuntu 26.04.1 (was 24.04). Nothing outside the repo survived (toolchain,
vcpkg + binary cache, ccache, runner, Docker images). Inventory and restore runbook:
`docs/project-context/host-toolchain.md`; executable form: `scripts/host-setup/setup-lab-host.sh`.

**Decision:** The lab host and all NF images run Ubuntu 26.04 with GCC 15.2, and the CI lint job moves from clang-tidy-18 to clang-tidy-22.

- Host compiler is the 26.04 default **GCC 15.2** (user choice over keeping g++-13). Measured: a cold
  Debug configure + full build (`-j4 -k 0`, 3532 steps, 102 vcpkg ports) completed with **zero
  compile errors** and all 27 NF binaries; 231 compiler warnings (top: -Wmissing-field-initializers 77,
  -Wconversion 64, -Wsign-conversion 33, -Wdeprecated-declarations 32). No 24.04 warning count was
  recorded, so how many are new under GCC 15 is unknown.
- Host and images must be the same OS (user-directed): all 31 NF Dockerfiles (builder + runtime) and
  compose `pki-init` move `ubuntu:24.04` -> `ubuntu:26.04`. Runtime packages renamed in 26.04:
  `libxml2` -> `libxml2-16`, `libelf1` -> `libelf1t64`. Upstream `postgres:16` and `node:22-alpine`
  unchanged.
- Found while checking runtime deps with `ldd` on the 26.04 build: `amf` and `udm` dynamically link
  `libsctp.so.1` but their runtime stages never installed `libsctp1` (only `chf` had, ADR-0062) --
  pre-existing, not caused by 26.04; `libsctp1` added to both. Also seen: nothing links libxml2
  dynamically any more (vcpkg links it statically), so the runtime `libxml2-16` is redundant but kept.
- Docker on the lab host is Docker CE from download.docker.com; the setup script must never install
  Ubuntu's `docker.io` alongside it (its first version did; apt removed docker-ce -- recovered).

**Lint measurement (clang-tidy, repo `.clang-tidy`, GCC 15 build's compile DB).** Wall seconds:

| file | 18 (CI today) | 18 without google-runtime-int | 21 | 22 |
|---|---|---|---|---|
| `nfs/nrf/src/main.cpp` (924 lines) | 475 | 154 | 156 | **34** |
| `nfs/udr/src/main.cpp` (10342 lines) | 595 | 269 | -- | **74** |
| `libs/sbi-core/src/problem_details.cpp` | 33 | -- | -- | **4.5** |

Disclosed: the 18/21/22 columns for each file ran concurrently (3-4 processes, as CI's `-P4` does);
the "without google-runtime-int" column ran alone, so it is if anything flattered. Single runs, no
repetitions. On the 24.04-era build (libstdc++ 13) clang-tidy-18 took **1777 s** on the same NRF file,
88% of it in `google-runtime-int`; on libstdc++ 15 that check is 75% (NRF) / 67% (UDR) -- its cost
depends heavily on the standard-library headers parsed. In clang-tidy 21 it costs ~2 s, in 22 ~0 s,
with the **same 3 findings** in every version (2 of them repeat from `sbi_core` headers in every TU).
Findings differ by version because checks evolve: on UDR, 22 adds
`cppcoreguidelines-pro-bounds-avoid-unchecked-container-access` (153), `portability-avoid-pragma-once`
(21; 1263 headers use `#pragma once`), `modernize-use-ranges` (9) and others, and reports fewer
`misc-const-correctness` (17 vs 59). Since `.clang-tidy` has `WarningsAsErrors: ''`, no finding from
any version fails the job -- only parse errors do (0 errors in every run above).

**Options for the lint job (user chose 2 on 2026-10-12).**
1. Keep clang-tidy-18, add `-google-runtime-int` to `.clang-tidy`: ~2.2-3.1x faster per file; loses 3
   style findings; no toolchain change, still works on the GitHub-hosted fallback.
2. Move CI to clang-tidy-22 (in the 26.04 archive, 22.1.2) and add `-portability-avoid-pragma-once`:
   ~7-14x faster per file; keeps every check; different finding set; `clang-format` stays `-18` unless
   also moved (format output can differ across versions). The `ubuntu-latest` fallback path would need
   apt.llvm.org for 22.
3. Both.

**Lint decision as implemented (option 2).**
- `ci.yml` lint job runs `clang-tidy-22` (`xargs -P` unchanged, same file selection). The self-hosted
  runner's tool check requires `clang-tidy-22`; build/sanitize jobs are untouched (they never run it).
- GitHub-hosted fallback: installs `clang-tidy-22` from the image's own archive when present, else adds
  apt.llvm.org's `llvm-toolchain-<codename>-22` repo (checked: noble-22 serves 22.1.8; the 26.04 archive
  has 22.1.2 -- a patch-level difference between the two runner kinds, accepted).
- `.clang-tidy`: `-portability-avoid-pragma-once`. With it, 22 enables 353 checks vs 302 for 18 on the
  same config. `google-runtime-int` stays on (in 22 it costs ~0 s). Also fixed while editing: a YAML
  comment placed inside the folded `Checks:` block becomes part of the check list -- the note is above it.
- `clang-format` stays at `-18` (not measured here; format output can differ between major versions).
- **GCC 15 broke clang-tidy for every file, any version -- found by the pre-merge sweep, fixed.** With
  GCC >= 14, CMake enables C++20 module dependency scanning and writes `-fmodules-ts`,
  `-fmodule-mapper=...` and `-fdeps-format=p1689r5` into every compile command; clang rejects them
  (`unknown argument`), so clang-tidy 18 *and* 22 returned non-zero on all 197 tracked files ("Found
  compiler error(s)") while still printing warnings. GCC 13 never triggered scanning, which is why CI's
  lint passed until now. The repo has no C++20 modules (0 `.cppm`/`.ixx`, no `module`/`import`), so the
  top-level `CMakeLists.txt` sets `CMAKE_CXX_SCAN_FOR_MODULES OFF` -- which also drops 562 scan steps
  from the 3532-step build. After reconfiguring: 0 of 2789 compile commands carry the flags, and
  clang-tidy 22 (problem_details 4 s, NRF main 26 s) and 18 (problem_details 20 s) exit 0.
  Disclosed: the per-file timings in the table above were taken *before* this fix (with the flag errors
  present -- the checks still ran, but they are not post-fix numbers), and my earlier "errors=0" counts
  only grepped stdout, so they missed these. The full 197-file sweep must be re-run on the fixed
  compile DB; not yet done at commit time.

**Rejected alternatives.**
- Option 1 (stay on 18, drop `google-runtime-int`): 2.2-3.1x only -- even without that check 18 stayed
  ~4.5x slower than 22 on NRF (154 s vs 34 s) and ~3.6x on UDR (269 s vs 74 s); kept as the fallback
  if 22 ever regresses.
- Keep GCC 13 on the host (`g++-13` is still in 26.04): rejected by the user in favour of the distro
  default; the full build showing zero errors under GCC 15 removed the main risk.
- Keep images on `ubuntu:24.04` (host OS independent): rejected by the user -- host and images must
  match OS and compiler.
- Reuse the restored `build*/` trees: impossible -- GCC 13 objects, a vcpkg tree built on 24.04 and
  binaries linked against 24.04's libbpf 1.3 etc. `build/` kept as `build-u24/` for reference.

**Open.** Lint option (above); Docker images not yet rebuilt or run on 26.04 (the `libsctp1` fix is
`ldd`-verified only); runner not re-registered; NWDAF venv works but its pip run had not exited;
`build-asan`/`build-tsan`/`build-release`/`build-fuzz` are stale 24.04 trees.
