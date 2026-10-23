#!/usr/bin/env bash
# Writes one Kubernetes Secret manifest per NF Helm chart, from the lab PKI (REVIEW-5ae0ef1 DEP-01,
# ADR-0513). Each chart mounts its own Secret, "<nf>-certs", at /build/certs with the layout
# CERTS_DIR expects; each Secret holds only that NF's material:
#   ca.crt              the lab root CA                  -> ca/ca.crt
#   cert.pem, key.pem   the NF's certificate and key     -> <nf>/cert.pem, <nf>/key.pem
#   nrf-jwt-public.pem  verifies NRF-issued tokens       -> nrf-jwt/public.pem
#   nrf-jwt-private.pem (nrf only) signs them            -> nrf-jwt/private.pem
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
done
