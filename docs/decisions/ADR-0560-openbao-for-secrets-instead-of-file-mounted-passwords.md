## ADR-0560: OpenBao for secrets instead of file-mounted passwords; HashiCorp Vault rejected on licence

**Date:** 2026-11-03. **Status:** Accepted.

**Origin.** From the reference-diagram review (HashiCorp Vault in the inference/RAG and
security boxes). Implemented after ADR-0556 to ADR-0558. See ADR-0556 "Origin".

**Context.** Secrets are files today: TLS keys and certificates under `CERTS_DIR`, per-NF Valkey
ACL passwords (`redis_password_file`, ADR-0514), the AAnF key-encryption key (ADR-0514). In the
lab they come from `gen-lab-pki.sh`; in Helm, from Kubernetes Secrets an operator creates by hand.
There is no rotation, no access audit and no lease.

**Decision:**
1. **OpenBao** (MPL-2.0, Linux Foundation-hosted fork of Vault taken before the licence change)
   becomes the secrets store for lab and Helm deployments.
2. **NF code does not change.** NFs keep reading secrets from files. An OpenBao Agent sidecar
   (Helm) or init step (Compose) authenticates the workload, fetches its secrets and renders them
   to a tmpfs path the NF already reads. No C++ links an OpenBao client.
3. First secrets moved: the Valkey ACL passwords, then the AAnF key-encryption key.
4. TLS certificates for NFs are not issued by OpenBao's PKI engine until the issued profile is
   checked against the 3GPP certificate profile (TS 33.310) and the NF-instance-ID SAN rules this
   project already enforces. Deferred for future implementation.
5. Kubernetes Secrets remain a supported fallback, so a deployment without OpenBao still works.

**Rejected alternatives.**
- HashiCorp Vault: moved to the Business Source License 1.1 in 2023, not OSI-approved.
- Kubernetes Secrets only: no rotation or access audit, and nothing for the Compose lab.
- Embedding an OpenBao client in each NF: adds a dependency and a failure path to every NF for
  no gain over rendered files.

**Disclosed.**
- Design only. Secret rotation needs each NF to re-read a file on change; whether every NF does
  that today has not been checked, and an NF that reads its password only at start-up would need
  a restart on rotation.
