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
| C/C++ compiler | GCC 13.3.0 (`/usr/bin/c++`, `build/CMakeCache.txt`) | **GCC 15.2**, the 26.04 default (user decision 2026-10-11); `g++-13` stays installed but unused | CMakeCompilerId of old build |
| clang | clang-18 (UPF XDP program; `find_program(clang clang-18)`) | apt `clang-18` (still in 26.04 archive, 18.1.8) | `nfs/upf/CMakeLists.txt` |
| clang-tidy / clang-format | `-18` pinned | apt `clang-tidy-18`, `clang-format-18` | `ci.yml` lint job |
| CMake | **4.4.0** in `~/.local/build-tools-venv` (not distro) | venv `pip install cmake==4.4.0` (distro is 4.2.3) | ADR-0452 |
| Ninja | apt `ninja-build` | apt (1.13.2) | `ci.yml` |
| ccache | 4.14 static in `~/.local/bin`, 20 GB, dir `~/.cache/5gc-ci/ccache` | apt `ccache` 4.12.3 is sufficient | ADR-0363 |
| vcpkg | `~/vcpkg` (dev) + `~/.cache/5gc-ci/vcpkg` (CI), both at `builtin-baseline` | script clones both at the baseline | `vcpkg.json`, `ci.yml` |
| vcpkg binary cache | `~/.cache/5gc-ci/vcpkg-cache` (~470 archives) | **lost** -- first configure rebuilds every port (hours) | ADR-0363 |
| asn1c 0.9.29 + APER patch | `build-tools/asn1c` (backup has it, 44 MB) | `scripts/setup-asn1c.sh` (needs autoconf/automake/libtool, bison, flex) | ADR-0030/0031 |
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
   `./config.sh --url https://github.com/prajithparan/AI-Boosted-5G-Advanced-Core-and-Charging-R19 --token <token> --labels 5gc-lab`
   (token: repo Settings -> Actions -> Runners -> New, or `gh api -X POST repos/<repo>/actions/runners/registration-token`).
   Remove the dead `5gc-lab` runner entry in the same page. Prefer `sudo ./svc.sh install && sudo ./svc.sh start`
   over `nohup` so it survives reboot (ADR-0363 disclosed this was never done).
7. Re-run CI run 38057786320's failed jobs.

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
