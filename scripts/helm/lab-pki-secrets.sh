#!/usr/bin/env bash
# Writes one Kubernetes Secret manifest per NF Helm chart, from the lab PKI (REVIEW-5ae0ef1 DEP-01,
# ADR-0513). Each chart mounts its own Secret, "<nf>-certs", at /build/certs with the layout
# CERTS_DIR expects; each Secret holds only that NF's material:
#   ca.crt              the lab root CA                  -> ca/ca.crt
#   cert.pem, key.pem   the NF's certificate and key     -> <nf>/cert.pem, <nf>/key.pem
#   nrf-jwt-public.pem  verifies NRF-issued tokens       -> nrf-jwt/public.pem
#   nrf-jwt-private.pem (nrf only) signs them            -> nrf-jwt/private.pem
#   valkey.password     (Valkey NFs) its ACL password    -> <nf>/valkey.password  (ADR-0514)
#   kakma.kek           (aanf only) seals KAKMA at rest  -> aanf/kakma.kek        (ADR-0514)
# plus, when any Valkey NF is listed, the Secret "valkey-users-acl" (users.acl) for the cluster's
# Valkey, which must run with `--aclfile` or every Valkey NF fails WRONGPASS at start (ADR-0571).
# Certificates come from scripts/gen-lab-pki.sh (generated into certs/ if missing, CA reused), so
# each names DNS:<nf> -- the chart's Service name. LAB ONLY: a production cluster brings its own CA
# (e.g. cert-manager) and fills Secrets with the same keys.
#
# usage: scripts/helm/lab-pki-secrets.sh [nf ...] | kubectl apply -n <namespace> -f -
#        (no arguments: every NF chart under deploy/helm)
set -euo pipefail

REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." &>/dev/null && pwd)"
CERTS_DIR="${REPO_ROOT}/certs"

NFS=("$@")
if [ ${#NFS[@]} -eq 0 ]; then
    for chart in "${REPO_ROOT}"/deploy/helm/*/Chart.yaml; do
        nf="$(basename "$(dirname "${chart}")")"
        # nwdaf-mtlf mounts the NWDAF's own Secret, nwdaf-certs (ADR-0517).
        case "${nf}" in keycloak | oam-gui-bff | nwdaf-mtlf) continue ;; esac
        NFS+=("${nf}")
    done
fi

"${REPO_ROOT}/scripts/gen-lab-pki.sh" nrf "${NFS[@]}" >&2

b64() { base64 -w0 <"$1"; }
need() { [ -f "$1" ] || { echo "lab-pki-secrets.sh: missing $1" >&2; exit 1; }; }
# The NFs on the lab Valkey; must match VALKEY_NFS in scripts/gen-lab-pki.sh.
is_valkey_nf() { case "$1" in amf | ausf | chf | nwdaf | dccf | adrf | li-mdf | li-mdf3 | mfaf | aanf) return 0 ;; esac; return 1; }

for nf in "${NFS[@]}"; do
    cat <<EOF
---
apiVersion: v1
kind: Secret
metadata:
  name: ${nf}-certs
  labels:
    app.kubernetes.io/name: ${nf}
    app.kubernetes.io/part-of: 5gc-lab
type: Opaque
data:
  ca.crt: $(b64 "${CERTS_DIR}/ca/ca.crt")
  cert.pem: $(b64 "${CERTS_DIR}/${nf}/cert.pem")
  key.pem: $(b64 "${CERTS_DIR}/${nf}/key.pem")
  nrf-jwt-public.pem: $(b64 "${CERTS_DIR}/nrf-jwt/public.pem")
EOF
    if [ "${nf}" = "nrf" ]; then
        echo "  nrf-jwt-private.pem: $(b64 "${CERTS_DIR}/nrf-jwt/private.pem")"
    fi
    # ADR-0514: the NF's Valkey ACL password (gen-lab-pki.sh writes one per Valkey NF) and the
    # AAnF's KAKMA KEK. The charts of these NFs mount the keys as required items (ADR-0571), so a
    # missing file stops this script here instead of leaving the pod in ContainerCreating.
    if is_valkey_nf "${nf}"; then
        need "${CERTS_DIR}/${nf}/valkey.password"
        echo "  valkey.password: $(b64 "${CERTS_DIR}/${nf}/valkey.password")"
    fi
    if [ "${nf}" = "aanf" ]; then
        need "${CERTS_DIR}/aanf/kakma.kek"
        echo "  kakma.kek: $(b64 "${CERTS_DIR}/aanf/kakma.kek")"
    fi
done

# ADR-0571: the ACL file those passwords belong to (users.acl, SHA-256 hashes only), for the Valkey
# the operator runs in the cluster (`valkey-server --aclfile`, mounted from this Secret). No chart
# in this repo deploys Valkey yet (datastores are deferred for future implementation, DEP-03).
for nf in "${NFS[@]}"; do
    if is_valkey_nf "${nf}"; then
        need "${CERTS_DIR}/valkey/users.acl"
        cat <<EOF
---
apiVersion: v1
kind: Secret
metadata:
  name: valkey-users-acl
  labels:
    app.kubernetes.io/name: valkey
    app.kubernetes.io/part-of: 5gc-lab
type: Opaque
data:
  users.acl: $(b64 "${CERTS_DIR}/valkey/users.acl")
EOF
        break
    fi
done
