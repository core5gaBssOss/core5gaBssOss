#!/usr/bin/env bash
# Generates a throwaway lab CA and per-NF leaf certificates for local TLS 1.3 + mTLS between NFs.
#
# NOT for production use as-is: this is a single self-signed root with no intermediate, no CRL/OCSP,
# no HSM-backed keys, and 1-year validity purely because "regenerate before it expires" is simpler
# than rotation machinery for a lab. Real deployment PKI (per TS 33.501) is out of scope here --
# this exists so libs/sbi-core's mTLS enforcement has real certs to test against. See
# docs/DECISIONS.md for the ADR this supports.
#
# Each NF gets ONE cert with BOTH serverAuth and clientAuth EKU, because every NF in this project
# is simultaneously an SBI server (exposing its own API) and an SBI client (calling other NFs) --
# see CLAUDE.md's non-negotiable rules. SAN covers 127.0.0.1/localhost since every NF in this lab
# runs on loopback; add real hostnames/IPs here if that ever changes.
#
# Usage: scripts/gen-lab-pki.sh [nf-name ...]
#   Defaults to: stub-nrf hello-nf
#   Output: certs/ca/ca.{key,crt}, certs/<nf-name>/{key,crt}.pem

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." &>/dev/null && pwd)"
CERTS_DIR="${REPO_ROOT}/certs"

NF_NAMES=("$@")
if [ ${#NF_NAMES[@]} -eq 0 ]; then
    NF_NAMES=(nrf hello-nf)
fi

mkdir -p "${CERTS_DIR}/ca"

echo "== Lab root CA =="
if [ ! -f "${CERTS_DIR}/ca/ca.key" ]; then
    openssl ecparam -name prime256v1 -genkey -noout -out "${CERTS_DIR}/ca/ca.key"
    openssl req -x509 -new -key "${CERTS_DIR}/ca/ca.key" \
        -days 365 -sha256 \
        -subj "/O=5gc-r19 Lab/CN=5gc-r19 Lab Root CA" \
        -addext "basicConstraints=critical,CA:true" \
        -addext "keyUsage=critical,keyCertSign,cRLSign" \
        -out "${CERTS_DIR}/ca/ca.crt"
    echo "  generated ${CERTS_DIR}/ca/ca.{key,crt}"
else
    echo "  ${CERTS_DIR}/ca/ca.key already exists, reusing (delete certs/ to force regeneration)"
fi

# ADR-0491: the SEPP is identified on N32 by its PLMN FQDN (TS 23.003 28.2), and the peer SEPP
# correlates an N32-f connection by the FQDN in this certificate (TS 29.573 5.3.3.2). The lab runs
# both partner SEPPs (PLMN 001/01 and 001/02) from one certificate, which therefore names both --
# the "common certificate" case of 5.3.3.2.1's NOTE, resolved by the Via header.
LAB_SEPP_FQDNS="${LAB_SEPP_FQDNS:-sepp.5gc.mnc001.mcc001.3gppnetwork.org sepp.5gc.mnc002.mcc001.3gppnetwork.org}"
extra_sans() {
    if [ "$1" = "sepp" ]; then
        # The wildcard serves telescopic FQDNs "<label>.<SEPP FQDN>" (TS 33.501 13.1, ADR-0492).
        for f in ${LAB_SEPP_FQDNS}; do printf ',DNS:%s,DNS:*.%s' "${f}" "${f}"; done
    fi
}

for nf in "${NF_NAMES[@]}"; do
    nf_dir="${CERTS_DIR}/${nf}"
    mkdir -p "${nf_dir}"

    if [ -f "${nf_dir}/key.pem" ]; then
        echo "== ${nf}: cert already exists, skipping =="
        # Group-readable (openssl otherwise writes private keys 0600, owner-only, regardless of
        # whether this branch or the fresh-generation branch below runs): every NF's own runtime
        # container in this project runs as root, so 0600-owned-by-root was never a problem here --
        # until Keycloak (ADR-0441/ADR-0442), whose official image drops privileges to UID 1000
        # (still GID 0, confirmed: `docker run --entrypoint id quay.io/keycloak/keycloak:26.0` ->
        # `uid=1000(keycloak) gid=0(root)`) and fails closed at startup unable to read a 0600
        # root-owned key.pem ("Failed to start server in (production) mode:
        # /build/certs/keycloak/key.pem", no further detail logged). This branch's own chmod is not
        # optional: a `certs_data` volume provisioned by an EARLIER run of this script (i.e. every
        # volume that already exists on a machine that ran ADR-0441's own compose Keycloak service
        # before this fix) hits this "already exists" branch forever after, never the fresh one
        # below -- meaning ADR-0441's own compose Keycloak could not actually have started against
        # such a volume as committed; this is disclosed in ADR-0442, not assumed fixed by the
        # fresh-generation branch alone.
        chmod 640 "${nf_dir}/key.pem" 2>/dev/null || true
        continue
    fi

    echo "== ${nf} =="
    openssl ecparam -name prime256v1 -genkey -noout -out "${nf_dir}/key.pem"
    # 0640 (owner rw, GROUP r, other none), not the openssl default 0600: every leaf key's group
    # owner is `root` (gid 0) in every container that runs this script, which is the SAME gid
    # Keycloak's own non-root UID 1000 process runs under -- group-read is therefore sufficient,
    # world-readable is not needed on this otherwise-shared, multi-user host. Still a lab-only
    # CA/keypair (this file's own header comment) -- the CA key itself (`ca.key`, which signs, and
    # is never read by a served container) is deliberately left at its default 0600.
    chmod 640 "${nf_dir}/key.pem"
    openssl req -new -key "${nf_dir}/key.pem" \
        -subj "/O=5gc-r19 Lab/CN=${nf}" \
        -out "${nf_dir}/csr.pem"

    openssl x509 -req -in "${nf_dir}/csr.pem" \
        -CA "${CERTS_DIR}/ca/ca.crt" -CAkey "${CERTS_DIR}/ca/ca.key" -CAcreateserial \
        -days 365 -sha256 \
        -extfile <(cat <<EOF
basicConstraints=CA:false
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth,clientAuth
subjectAltName=DNS:${nf},DNS:localhost,IP:127.0.0.1$(extra_sans "${nf}")
EOF
) \
        -out "${nf_dir}/cert.pem"

    rm -f "${nf_dir}/csr.pem"
    echo "  generated ${nf_dir}/{key,cert}.pem (serverAuth+clientAuth, SAN=127.0.0.1/localhost/${nf}$(extra_sans "${nf}"))"
done

echo "== NRF OAuth2 JWT signing key (ES256) =="
# Deliberately a SEPARATE keypair from any NF's mTLS transport cert above -- reusing a transport
# key for JOSE signing mixes usages that real PKI practice (and TS 33.501) keeps apart. This is
# NRF's own signing key; every NF's copy of nrf-jwt/public.pem is what they use to verify tokens
# NRF issues (see sbi_core::jwt::Verifier). See docs/DECISIONS.md ADR-0012.
JWT_KEY_DIR="${CERTS_DIR}/nrf-jwt"
mkdir -p "${JWT_KEY_DIR}"
if [ ! -f "${JWT_KEY_DIR}/private.pem" ]; then
    openssl ecparam -name prime256v1 -genkey -noout -out "${JWT_KEY_DIR}/private.pem"
    openssl ec -in "${JWT_KEY_DIR}/private.pem" -pubout -out "${JWT_KEY_DIR}/public.pem" 2>/dev/null
    echo "  generated ${JWT_KEY_DIR}/{private,public}.pem"
else
    echo "  ${JWT_KEY_DIR}/private.pem already exists, reusing"
fi

has_nf() { printf '%s\n' "${NF_NAMES[@]}" | grep -qx "$1"; }

# ADR-0514 (REVIEW-5ae0ef1 SEC-04, TS 33.117 4.2.3.2.3): the AAnF seals every KAKMA it stores with
# this AES-256 key-encryption key (64 hex digits). Only the AAnF reads it.
if has_nf aanf; then
    echo "== AAnF KAKMA key-encryption key =="
    if [ ! -f "${CERTS_DIR}/aanf/kakma.kek" ]; then
        (umask 027 && openssl rand -hex 32 > "${CERTS_DIR}/aanf/kakma.kek")
        echo "  generated ${CERTS_DIR}/aanf/kakma.kek"
    else
        echo "  ${CERTS_DIR}/aanf/kakma.kek already exists, reusing"
    fi
fi

# ADR-0514: one Valkey ACL user per NF that uses the lab's single-node Valkey, each with its own
# random password (certs/<nf>/valkey.password, read via the NF's redis_password_file). The ACL
# file holds only SHA-256 hashes, so it is world-readable for the Valkey image's non-root user.
# The default (unauthenticated) user is off, so an NF without its credentials fails at its startup
# PING (nf_config::connect_redis_or_die), not at its first command. The AAnF's user is
# confined to its own keys; every other user is kept to logical database 0 (no SELECT, MOVE,
# COPY, SWAPDB) while the AAnF's contexts are in database 1, so no other NF can read them.
VALKEY_NFS=(amf ausf chf nwdaf dccf adrf li-mdf li-mdf3 mfaf aanf)
valkey_users=()
for nf in "${VALKEY_NFS[@]}"; do
    has_nf "${nf}" && valkey_users+=("${nf}")
done
if [ ${#valkey_users[@]} -gt 0 ]; then
    echo "== Valkey ACL users =="
    mkdir -p "${CERTS_DIR}/valkey"
    acl="user default off resetchannels -@all"
    for nf in "${valkey_users[@]}"; do
        pw_file="${CERTS_DIR}/${nf}/valkey.password"
        if [ ! -f "${pw_file}" ]; then
            (umask 027 && openssl rand -hex 32 > "${pw_file}")
        fi
        hash="$(tr -d '\n' < "${pw_file}" | openssl dgst -sha256 -r | cut -d' ' -f1)"
        if [ "${nf}" = "aanf" ]; then
            rules="resetkeys ~aanf:* resetchannels +@all -@admin -@dangerous"
        else
            rules="resetkeys ~* resetchannels +@all -@admin -@dangerous -select -move -copy"
        fi
        acl+=$'\n'"user ${nf} on #${hash} ${rules}"
    done
    printf '%s\n' "${acl}" > "${CERTS_DIR}/valkey/users.acl"
    chmod 644 "${CERTS_DIR}/valkey/users.acl"
    echo "  wrote ${CERTS_DIR}/valkey/users.acl (${valkey_users[*]})"
fi

echo "== Done. CA: ${CERTS_DIR}/ca/ca.crt =="
