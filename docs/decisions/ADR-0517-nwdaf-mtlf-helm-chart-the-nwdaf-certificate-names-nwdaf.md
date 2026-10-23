## ADR-0517: NWDAF MTLF Helm chart; the NWDAF certificate names nwdaf-mtlf (DEP-03 follow-up)

**Date:** 2026-10-23. **Status:** Proposed.

**Context.** ADR-0515 left the NWDAF MTLF without a chart. Compose runs the NWDAF image a second
time as `nwdaf-mtlf`: `NWDAF_ROLE=mtlf`, port 7797, metrics 9486, the training sidecar's paths
(`NWDAF_TRAINING_*`) and a volume for `/var/lib/nwdaf/training`. The AnLF's config names it
`https://nwdaf-mtlf:7797` (`mtlf_base_url`).

While building the chart: the MTLF is the NWDAF binary, so it serves `CERTS_DIR/nwdaf/cert.pem`,
whose SANs are `DNS:nwdaf, DNS:localhost, IP:127.0.0.1` (`scripts/gen-lab-pki.sh`). The SBI client
verifies the host name (`CURLOPT_SSL_VERIFYHOST 2`, `libs/sbi-core/src/http2_client.cpp`), so the
AnLF's calls to `https://nwdaf-mtlf:7797` fail TLS verification -- in compose today and in any
chart. Reproduced with curl against a server presenting the lab `nwdaf` certificate: "no
alternative certificate subject name matches target hostname 'nwdaf-mtlf'"; as `nwdaf`, 200.

**Decision:**
- `scripts/gen-lab-pki.sh`: the `nwdaf` certificate also names `DNS:nwdaf-mtlf` (in `extra_sans`,
  beside the SEPP's FQDNs). The MTLF is the same NF with the same identity (CN=nwdaf, one NRF
  profile type), so one certificate naming both Services is the fit, not a second identity.
- The same script now regenerates an existing certificate that lacks a name in `extra_sans`
  (`lacks_extra_sans`); before, an existing key meant "skip" forever, so a lab volume would never
  pick the new name up. Only certificates with extra names are checked (today the SEPP's and the
  NWDAF's).
- `deploy/helm/nwdaf-mtlf`: the NWDAF image with the generated config mounted as
  `/build/config/nwdaf.json` (the file the binary reads), the NWDAF's own Secret `nwdaf-certs`
  (`scripts/helm/lab-pki-secrets.sh` skips the chart, so no `nwdaf-mtlf` certificate is made),
  `NWDAF_ADVERTISED_IPV4` from the pod IP, Service `nwdaf-mtlf` on 7797/9486, a ReadWriteOnce
  PersistentVolumeClaim (default 1Gi, the cluster's default StorageClass) at
  `/var/lib/nwdaf/training`, and `strategy: Recreate` so two pods never hold the volume.
- `tools/lab/gen_helm_config.py`: `BINARY_OF = {'nwdaf-mtlf': 'nwdaf'}` (the compose variables are
  `NWDAF_*`), and a compose variable `<NF>_<SECTION>_<KEY>` now sets a key of a config section,
  as the NWDAF reads `training.python` from `NWDAF_TRAINING_PYTHON`. No other chart config
  changed (`--check` passes on the 34 existing ones).

**Rejected alternatives.**
- *A separate `nwdaf-mtlf` certificate.* The binary opens `CERTS_DIR/nwdaf/` whatever its role,
  and a CN other than `nwdaf` would be a second identity for one NF, which the NRF's
  certificate-bound token issuing (ADR-0499) does not expect.
- *Pointing the AnLF at `https://nwdaf:7797`.* `nwdaf` is the AnLF's own Service; the MTLF needs its
  own name to be reachable separately.
- *A role switch in the NWDAF chart (one chart, two releases).* Possible, but compose has two
  services and ADR-0513 one chart per deployable; the MTLF also needs the volume the AnLF does not.

**Disclosed.**
- **Not run in a cluster** (no kind/kubectl/helm on the host). `helm lint` and `helm template` only.
- **The AnLF -> MTLF call itself was not exercised** in compose or Helm (it needs Valkey, Doris,
  the NRF, ADRF and DCCF up and an analytics request that needs a model). What was verified is the
  TLS name check the call failed on.
- Lab users' existing `nwdaf` key and certificate are replaced on the next pki-init run; the NWDAF
  registers with the new certificate at its next start.
- Datastores and the ADRF/DCCF are not provisioned by the chart (as ADR-0513/0515).
- The MTLF does not expose LI_X1: `li_poi` is disabled in the config, and IRI comes from the AnLF's
  subscriptions and queries; enabling `li_poi` in this chart's config would also start a POI here.
- One replica; the ReadWriteOnce volume ties it to one node at a time.

**Verified (2026-10-23).**
- curl (the SBI client's libcurl check) against `openssl s_server` with the new certificate:
  `https://nwdaf-mtlf` -> 200 (before: curl error 60, above).
- `gen-lab-pki.sh` on a copy of the lab `certs/`: the old `nwdaf` certificate is regenerated with
  `DNS:nwdaf-mtlf`; the SEPP's (wildcard names present) and the AMF's are kept; a second run
  changes nothing.
- `docker compose ... run pki-init` on the lab `certs_data` volume: "nwdaf: cert lacks a name in
  extra_sans, regenerating"; the volume's certificate then names `DNS:nwdaf-mtlf`; SEPP kept.
- `alpine/helm:3.16.2`: `helm lint` passes; `helm template` renders a ConfigMap (key `nwdaf.json`,
  `role: mtlf`, port 7797, training paths from compose, no `advertised_ipv4`), a Deployment
  (Recreate; config, certs `nwdaf-certs`, training PVC mounts), a Service `nwdaf-mtlf` on
  7797/9486 and the PVC; `persistence.storageClassName` renders when set.
- `gen_helm_config.py --check`: 35 chart configs current.
