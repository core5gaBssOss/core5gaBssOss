# Lab host toolchain: inventory and disaster-recovery runbook

Written 2026-10-11, after the lab laptop crashed mid-CI and the repository was restored from a
snapshot onto a fresh **Ubuntu 26.04.1 LTS** (kernel 7.0). The machine had been Ubuntu 24.04.
Nothing outside the repo survived: no compiler, no CMake, no vcpkg, no ccache, no asn1c, no
GitHub Actions runner, no Docker images. This file is the record that lets the next restore be
mechanical. The executable form is `scripts/host-setup/setup-lab-host.sh`; keep the two in sync.

## What the crash took down

- **CI run 38057786320** (PR, `chore/token-optimization` @ `116b8ee`): `lint` passed (4 h 18 min),
  `build` compiled and configured cleanly, then died in **Test** with *"The self-hosted runner lost
  communication with the server"*. That failure is the crash, not a code defect; the commit has not
  actually been tested yet.
- **CI run 38128541443** (weekly `schedule` full sweep on `main`): all four jobs **queued** waiting for
  a runner labelled `5gc-lab`. It and every later run will wait until a runner is re-registered.

## Inventory (where each item lived, how it is restored)

| Item | Pre-crash (24.04) | Restore on 26.04 | Source of truth |
|---|---|---|---|
| C/C++ compiler | GCC 13.3.0 (`/usr/bin/c++`, `build/CMakeCache.txt`) | **GCC 15.2**, the 26.04 default (user decision 2026-10-11), for all NF code, everywhere -- including asn1c's own compiler build (see the asn1c row below for why that needed a build-flag fix, not a different compiler, as of ADR-0483). `g++-13` stays installed but unused. | CMakeCompilerId of old build; ADR-0483 |
| clang | clang-18 (UPF XDP program; `find_program(clang clang-18)`) | apt `clang-18` (still in 26.04 archive, 18.1.8) | `nfs/upf/CMakeLists.txt` |
| clang-tidy / clang-format | `-18` pinned | apt `clang-tidy-18`, `clang-format-18` | `ci.yml` lint job |
| CMake | **4.4.0** in `~/.local/build-tools-venv` (not distro) | venv `pip install cmake==4.4.0` (distro is 4.2.3) | ADR-0452 |
| Ninja | apt `ninja-build` | apt (1.13.2) | `ci.yml` |
| ccache | 4.14 static in `~/.local/bin`, 20 GB, dir `~/.cache/5gc-ci/ccache` | apt `ccache` 4.12.3 is sufficient | ADR-0363 |
| vcpkg | `~/vcpkg` (dev) + `~/.cache/5gc-ci/vcpkg` (CI), both at `builtin-baseline` | script clones both at the baseline | `vcpkg.json`, `ci.yml` |
| vcpkg binary cache | `~/.cache/5gc-ci/vcpkg-cache` (~470 archives) | **lost** -- first configure rebuilds every port (hours) | ADR-0363 |
| asn1c 0.9.29 + APER patch | `build-tools/asn1c` (backup has it, 44 MB) | `scripts/setup-asn1c.sh` (needs autoconf/automake/libtool, bison, flex). **Built with `CFLAGS="-g -O2 -fno-strict-aliasing"`**, every GCC version -- vanilla asn1c 0.9.29's own `libasn1fix/asn1fix_constr.c` has a real strict-aliasing UB (three nominally-distinct-but-identical anonymous `TQ_HEAD()` struct types aliased through `(void*)`) that GCC 15.2's optimizer exploits into a segfault generating the NGAP codec from `specs/NGAP/ngap-17.9.asn`; GCC 13.3 happened not to exploit it (the pre-crash Aug-7 binary silently avoided this only because it predates the GCC-15 move and `setup-asn1c.sh` is idempotent, not because GCC 13 is specially safe). `-fno-strict-aliasing` fixes it on any compiler version, so no compiler pin is needed. Reproduced and root-caused (gdb backtrace) on this host 2026-10-12. | ADR-0030/0031/**0483** |
| libpq build deps | bison, flex | apt | ADR-0054 |
| NF system libs | libsctp-dev, libbpf-dev, libcap-dev | apt (+ libelf-dev, zlib1g-dev for libbpf) | `ci.yml` |
| sbi-codegen | jinja2, pyyaml | in build-tools venv (PEP 668 blocks bare `pip` on 26.04) | `tools/sbi-codegen/requirements.txt` |
| NWDAF training | `~/.local/nwdaf-training-venv` | script creates it | ADR-0369 |
| Docker | **Docker CE** 29.9.0 from download.docker.com (`/etc/apt/sources.list.d/docker.sources`), compose plugin 5.6.0, buildx plugin 0.38.0, user in `docker` group | `docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin`; re-login. **Never** add Ubuntu's `docker.io`/`docker-compose-v2`/`docker-buildx` on top: on 2026-10-11 the first version of the setup script did, and apt removed `docker-ce` + `containerd.io` (recovered with `apt-get remove docker.io containerd` then reinstalling Docker CE). The script now picks Docker CE whenever its repo is configured. | ADR-0452 |
| Docker images | 31 Dockerfiles `FROM ubuntu:24.04` (builder + runtime), compose `pki-init` on `ubuntu:24.04` | **moved to `ubuntu:26.04`** so host and images are the same OS and compiler (user decision 2026-10-11). Runtime packages renamed in 26.04: `libxml2` -> `libxml2-16`, `libelf1` -> `libelf1t64`. Upstream images unchanged: `postgres:16` (postgres-chf, Debian-based) and `node:22-alpine` (GUI web build stage only). All images rebuild. | `deploy/docker/*.Dockerfile`, `docker-compose.yml` |
| UPF diagnostics | tcpdump, ip, bpftool; setcap'd copies `build/ip-priv`, `build/tcpdump-priv` | apt; re-copy from 26.04's `/usr/sbin` and re-run setcap manually (verified 2026-10-11: the restored copies carry **no** `security.capability` xattr, and are 24.04 binaries) | ADR-0043 |
| Narrow sudoers | `NOPASSWD` for exactly `/usr/sbin/ip`, `/usr/sbin/bpftool`, `/usr/sbin/setcap` | **manual**, user decision (`visudo`) | ADR-0043 |
| GitHub runner | `~/actions-runner`, 2.337.0, labels `self-hosted,linux,x64,5gc-lab`, started with `nohup` | **manual** (needs a registration token) -- see below | ADR-0363 |
| gh CLI | (not recorded) | apt `gh` | -- |
| git push credentials | (not recorded pre-crash; likely a cached credential helper) | **lost** -- restored git has no credential helper at all, so even `gh`-authenticated pushes fail; see "git/gh credential restore" below | this session, 2026-10-12 |

## Restore order

1. `sudo scripts/host-setup/setup-lab-host.sh system`, then log out/in (docker group).
2. `scripts/host-setup/setup-lab-host.sh user` (no sudo; vcpkg clones + asn1c build).
3. Put `~/.local/build-tools-venv/bin:~/.local/bin` first on `PATH` (`~/.bashrc`).
4. `scripts/host-setup/setup-lab-host.sh verify` -- must print no `MISS`.
5. Reconfigure from scratch: the restored `build*/` trees hold GCC 13.3 objects, a vcpkg tree built on
   24.04, and binaries linked against 24.04's system libraries -- none of it is reusable. On
   2026-10-11 `build/` was moved to `build-u24/` (reference only; `build-asan`, `build-tsan`,
   `build-release`, `build-fuzz` are equally stale). Build detached (skill `5gc-build-and-verify`).
6. Runner: download `actions/runner` into `~/actions-runner`, then
   `./config.sh --url https://github.com/core5gaBssOss/core5gaBssOss --token <token> --labels 5gc-lab`
   (token: repo Settings -> Actions -> Runners -> New, or `gh api -X POST repos/<repo>/actions/runners/registration-token`).
   Remove the dead `5gc-lab` runner entry in the same page. Prefer `sudo ./svc.sh install && sudo ./svc.sh start`
   over `nohup` so it survives reboot (ADR-0363 disclosed this was never done).
7. Re-run CI run 38057786320's failed jobs.

## git/gh credential restore (2026-10-12)

Discovered this session, not covered by `setup-lab-host.sh` (the device-code login step is
interactive and cannot be scripted with a secret baked in): after the restore, `git push` over
HTTPS failed with `fatal: could not read Username for 'https://github.com'` -- the previous
credential helper did not survive the crash, and the repo was never re-authenticated.

1. `gh auth login` -- device flow (prints a one-time code + `https://github.com/login/device`;
   a human must open that URL and enter the code). Logged in as `prajithparan`, default scopes
   `gist, read:org, repo`.
2. `gh auth setup-git` -- wires `gh` in as git's credential helper. Without this step `git push`
   still fails the same way even though `gh auth status` shows logged in; `gh` login alone does not
   configure git.
3. First push after that was still rejected: `refusing to allow an OAuth App to create or update
   workflow '.github/workflows/ci.yml' without 'workflow' scope` -- any push whose diff touches a
   file under `.github/workflows/` needs the `workflow` OAuth scope, which `gh auth login`'s default
   scope set does not include.
4. `gh auth refresh -h github.com -s workflow` -- second device-code flow, additive (keeps the
   existing scopes and adds `workflow`). After this, `git push` succeeded.

Net: on a freshly restored host, a push that touches `.github/workflows/*` needs steps 1+2+4, not
just 1+2; a push that doesn't touch workflow files only needs 1+2.

## Docker image rebuild strategy (2026-10-12, in progress -- see ADR-0482/commit `d8e7910` for the
## ubuntu:26.04 migration itself)

The 32 `docker compose build`-able services (31 project Dockerfiles; `nwdaf`/`nwdaf-mtlf` share one)
all moved `FROM ubuntu:26.04` but, per that commit's own disclosure, were **not** rebuilt as part of
the migration commit -- only the Dockerfiles were edited. Rebuilding them is what actually proves the
migration, and is the long step after a crash restore (cold vcpkg-bincache, same as the host build).

Observed approach, in two phases:
1. **Build the first image (`nrf`) alone, serially.** Every Dockerfile shares one `vcpkg.json`
   manifest and the same BuildKit cache-mount ids (`vcpkg-bincache`, `ccache`, both
   `sharing=locked`), so the first image's vcpkg port compiles (boost, openssl, protobuf, onnxruntime,
   ...) populate the cache the other 31 images will then reuse. Building it alone also avoids 8+
   concurrent cold vcpkg bootstraps competing for the same lock from an empty cache.
2. **Build the remaining images with bounded parallelism**, once the first is done:
   `COMPOSE_PARALLEL_LIMIT=3 docker compose build --parallel <remaining services>`. Limit chosen for
   an 8-core / ~14 GB host: the `apt-get`/`git clone`/asn1c-build setup stages in each Dockerfile are
   I/O/network-bound (near-0% CPU use observed during them, confirmed with `top` -- not a stall), so
   running several of those concurrently is close to free wall-clock time; the actual `cmake --build`
   step is the CPU/RAM-heavy part, and `sharing=locked` on the cache mounts naturally serializes that
   part across the parallel builds anyway, so parallelism here mainly overlaps the cheap setup
   stages, not the expensive compile ones. A higher limit risks OOM on the heavier linkers (`chf`,
   `nwdaf` link ONNX Runtime).
3. Per-image pass/fail is checked after the fact with `docker images -q 5gc-r19-<service>` for each
   service (`docker compose build --parallel`'s own exit code says *that something* failed, not
   which image).

Not yet done as of this writing: the GitHub Actions runner re-registration (restore-order step 6
above) and the actual CI re-run (step 7) -- both are blocked on the image rebuild finishing first,
since the CI run that matters for this branch is still queued on the self-hosted `5gc-lab` label with
no runner online.

## What changed between 24.04 and 26.04 that matters here

- Default `g++` is 15.2 (was 13.3) and is now the project compiler on host and in images. GCC 15 is
  stricter about transitive includes; expect missing-`#include` fixes on the first build.
- Default `clang`/`clang-tidy` (unversioned) is 21; this repo always names `-18` explicitly, so
  nothing silently switches -- **except** clang-18 picks the *newest* installed GCC's libstdc++.
  With `g++` 15 installed, clang-tidy-18 parses libstdc++ 15, not 13. Measured effect below.
- Python 3.14 (was 3.12): checked 2026-10-11 that onnx (abi3), scikit-learn, numpy, skl2onnx and
  mlflow all publish 3.14-compatible wheels. mlflow's dependency tree is large; the first install is slow.
- PEP 668: a bare `pip install` is refused, so the build-tools venv on `PATH` is now load-bearing for
  `ci.yml`'s self-hosted "Install sbi-codegen Python deps" step.
- Distro CMake 4.2.3 would be found if the venv is not first on `PATH` (ADR-0452 version sensitivity).

## Lint (clang-tidy) cost on 26.04

Measured 2026-10-11, full table and options in ADR-0482. In short: clang-tidy-18 spends 67-88% of its
time in `google-runtime-int` (3 findings); clang-tidy-22 (26.04 archive) is ~7-14x faster per file
with the same 3 findings. The extracted 21/22 binaries used for the measurement live in
`build-tools/lint-cmp/` (gitignored).
