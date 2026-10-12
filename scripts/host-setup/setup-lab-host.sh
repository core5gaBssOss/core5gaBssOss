#!/usr/bin/env bash
# Rebuilds the 5gc-lab developer/CI host toolchain from a bare Ubuntu install.
#
# Written 2026-10-11 after the lab laptop crashed and the repo was restored onto a fresh
# Ubuntu 26.04.1 (previously 24.04). The full inventory, with the provenance of every item and
# what changed between 24.04 and 26.04, is docs/project-context/host-toolchain.md -- keep the two
# in sync. Idempotent: every step checks before acting, so it is safe to re-run.
#
# Usage:
#   sudo scripts/host-setup/setup-lab-host.sh system   # apt packages + docker group (root)
#   scripts/host-setup/setup-lab-host.sh user          # venvs, vcpkg, asn1c, caches (NO sudo)
#   scripts/host-setup/setup-lab-host.sh verify        # prints what is present / missing
#
# Not automated on purpose (see host-toolchain.md): GitHub Actions runner registration (needs a
# registration token from the repo owner), the narrow NOPASSWD sudoers grant of ADR-0043, and the
# setcap'd ip/tcpdump copies under build/.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CMAKE_VERSION="4.4.0"   # ADR-0452: every build of this project configures under 4.4.0, not distro cmake
CI_CACHE="${HOME}/.cache/5gc-ci"

# apt packages. One line per purpose so a future diff shows why each was added.
APT_PACKAGES=(
    # compilers: the project and vcpkg build with the distro c++; clang-18 compiles the UPF's XDP
    # program (nfs/upf/CMakeLists.txt) and is the version CI's clang-tidy/clang-format are pinned to
    build-essential g++-13 gcc-13 clang-18 clang-tidy-18 clang-format-18 lld-18
    # clang-tidy-22: CI's lint job since ADR-0482 (7-14x faster than 18 on this tree)
    clang-tidy-22
    # build drivers (cmake itself comes from the build-tools venv below, pinned to CMAKE_VERSION)
    ninja-build make pkg-config ccache git curl zip unzip tar patch
    # vcpkg ports that build from source need these (libpq: bison/flex, ADR-0054)
    autoconf automake autoconf-archive libtool bison flex linux-libc-dev
    # system libs the NFs link against outside vcpkg
    libsctp-dev libbpf-dev libcap-dev libelf-dev zlib1g-dev
    # python: sbi-codegen, NWDAF/CHF training sidecars, repo checks
    python3 python3-venv python3-pip python3-yaml
    # containers: chosen in docker_packages() below -- never mix Ubuntu's docker.io with Docker CE
    # diagnostics used during UPF/N3 work (ADR-0043)
    tcpdump iproute2 bpftool libcap2-bin
    # DB clients for ad-hoc lab work (CI itself docker-execs into service containers)
    postgresql-client mariadb-client
    # GitHub CLI (PR/run inspection)
    gh jq
)

need_root() { [[ $EUID -eq 0 ]] || { echo "run '$1' with sudo" >&2; exit 1; }; }
no_root()   { [[ $EUID -ne 0 ]] || { echo "run '$1' as your normal user, not root" >&2; exit 1; }; }

# Docker (Compose v2 + buildx; ADR-0452 BuildKit cache mounts need buildx). The lab host runs
# Docker CE from download.docker.com. Ubuntu's docker.io/docker-compose-v2/docker-buildx conflict
# with it: on 2026-10-11 an earlier version of this script installed them and apt REMOVED docker-ce
# and containerd.io, leaving docker.io half-configured. So: Docker CE if its repo is configured or it
# is already installed; Ubuntu's packages only on a host with neither.
docker_packages() {
    if ls /etc/apt/sources.list.d/ 2>/dev/null | grep -q '^docker\.' \
       || dpkg-query -W -f='${Status}' docker-ce 2>/dev/null | grep -q 'install ok installed'; then
        echo docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
    else
        echo docker.io docker-compose-v2 docker-buildx
    fi
}

system_step() {
    need_root system
    apt-get update -qq
    # shellcheck disable=SC2046
    apt-get install -y --no-install-recommends "${APT_PACKAGES[@]}" $(docker_packages)
    local u="${SUDO_USER:-}"
    if [[ -n "$u" ]] && ! id -nG "$u" | grep -qw docker; then
        usermod -aG docker "$u"
        echo "added $u to the docker group -- log out and back in (or 'newgrp docker')"
    fi
    systemctl enable --now docker >/dev/null 2>&1 || true
}

user_step() {
    no_root user
    mkdir -p "${HOME}/.local/bin" "${CI_CACHE}"

    # build-tools venv: cmake pinned + sbi-codegen deps. CI puts its bin/ on PATH (ci.yml, ADR-0363),
    # and on Ubuntu >= 23.04 a bare `pip install` is refused (PEP 668), so this venv is also what
    # makes ci.yml's "pip install -r tools/sbi-codegen/requirements.txt" work on the self-hosted runner.
    local venv="${HOME}/.local/build-tools-venv"
    [[ -x "${venv}/bin/python3" ]] || python3 -m venv "${venv}"
    "${venv}/bin/pip" install -q --upgrade pip
    "${venv}/bin/pip" install -q "cmake==${CMAKE_VERSION}" -r "${ROOT_DIR}/tools/sbi-codegen/requirements.txt"

    # NWDAF training sidecar venv (ADR-0369); ci.yml creates it on demand, done here so local ctest works.
    local nw="${HOME}/.local/nwdaf-training-venv"
    [[ -x "${nw}/bin/python3" ]] || python3 -m venv "${nw}"
    "${nw}/bin/pip" install -q -r "${ROOT_DIR}/nfs/nwdaf/training/requirements.txt" \
        || echo "WARN: NWDAF training deps failed to install (check wheel availability for $(python3 -V))"

    # vcpkg at vcpkg.json's builtin-baseline -- two checkouts, as before the crash:
    #   ~/vcpkg               (the developer build's CMAKE_TOOLCHAIN_FILE, build/CMakeCache.txt)
    #   ~/.cache/5gc-ci/vcpkg (the self-hosted runner's VCPKG_ROOT, ci.yml)
    local baseline
    baseline=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["builtin-baseline"])' "${ROOT_DIR}/vcpkg.json")
    for vroot in "${HOME}/vcpkg" "${CI_CACHE}/vcpkg"; do
        [[ -d "${vroot}/.git" ]] || git clone -q https://github.com/microsoft/vcpkg.git "${vroot}"
        if [[ "$(git -C "${vroot}" rev-parse HEAD)" != "${baseline}" ]]; then
            git -C "${vroot}" fetch -q origin && git -C "${vroot}" checkout -q "${baseline}"
            rm -f "${vroot}/vcpkg"
        fi
        [[ -x "${vroot}/vcpkg" ]] || "${vroot}/bootstrap-vcpkg.sh" -disableMetrics
    done
    mkdir -p "${CI_CACHE}/vcpkg-cache" "${CI_CACHE}/ccache" "${CI_CACHE}/build-tools"

    # patched asn1c (ADR-0030/0031) into the repo's build-tools/
    "${ROOT_DIR}/scripts/setup-asn1c.sh"

    echo "user step done. Add to ~/.bashrc:  export PATH=\"\$HOME/.local/build-tools-venv/bin:\$HOME/.local/bin:\$PATH\""
}

verify_step() {
    export PATH="${HOME}/.local/build-tools-venv/bin:${HOME}/.local/bin:${PATH}"
    local rc=0
    for t in gcc g++ g++-13 clang-18 clang-tidy-18 clang-tidy-22 clang-format-18 cmake ninja ccache bison flex \
             pkg-config docker gh jq python3 bpftool tcpdump; do
        if command -v "$t" >/dev/null; then printf '  ok   %-16s %s\n' "$t" "$("$t" --version 2>&1 | head -1)"
        else printf '  MISS %s\n' "$t"; rc=1; fi
    done
    for p in libsctp libbpf libcap; do
        pkg-config --exists "$p" 2>/dev/null && echo "  ok   pkg:$p" || { echo "  MISS pkg:$p"; rc=1; }
    done
    [[ "$(cmake --version 2>/dev/null | head -1)" == *"${CMAKE_VERSION}"* ]] \
        || { echo "  WARN cmake is not ${CMAKE_VERSION} (ADR-0452)"; rc=1; }
    [[ -x "${ROOT_DIR}/build-tools/asn1c/bin/asn1c" ]] && echo "  ok   asn1c" || { echo "  MISS asn1c (scripts/setup-asn1c.sh)"; rc=1; }
    [[ -x "${HOME}/vcpkg/vcpkg" ]] && echo "  ok   ~/vcpkg" || { echo "  MISS ~/vcpkg"; rc=1; }
    docker info >/dev/null 2>&1 && echo "  ok   docker daemon reachable" || { echo "  MISS docker access (docker group?)"; rc=1; }
    [[ -d "${HOME}/actions-runner" ]] && echo "  ok   ~/actions-runner present" || echo "  INFO no ~/actions-runner (manual, see host-toolchain.md)"
    return $rc
}

case "${1:-}" in
    system) system_step ;;
    user)   user_step ;;
    verify) verify_step ;;
    *) sed -n '2,20p' "$0"; exit 2 ;;
esac
