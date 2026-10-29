## ADR-0513: NF Helm charts start their NF: generated config ConfigMap, per-NF certs Secret, Service named after the NF (REVIEW-5ae0ef1 DEP-01)

**Date:** 2026-10-23. **Status:** Accepted.

**Context.** REVIEW-5ae0ef1 DEP-01 (High): none of the NF Helm charts could start its NF. The
images hold only the binary, which reads `/build/config/<nf>.json` and its TLS material under
`/build/certs` (CERTS_DIR). Of the 12 NF charts (`aanf`, `amf`, `ausf`, `li-admf`, `nrf`, `pcf`,
`sepp`, `smf`, `tsctsf`, `udm`, `udr`, `upf`), 11 mounted neither; `upf` mounted a hand-written
copy of its config but no certificates. Each chart's description deferred the PKI question to
"Phase 8". This is ADR-0453's failure class, in Helm.

**Decision:**
1. **Config.** `tools/lab/gen_helm_config.py` writes `deploy/helm/<nf>/files/<nf>.json` from the
   compose lab config (`deploy/docker/lab/config/<nf>.json`, ADR-0503, itself generated from
   `config/`). That file already names every peer and datastore by its service name. The only
   change is `advertised_ipv4`, because a pod's IP is known only at runtime:
   - `upf`: `"auto"` (ADR-0466).
   - `amf`: the key is removed, and the chart sets `AMF_ADVERTISED_IPV4` from `status.podIP`.
     `nf_config::require` reads the variable first, so if it is ever missing, AMF fails fast
     rather than registering a stale address.
   - Any other NF that has the key stops the generator until it is given a rule.

   Each chart renders a ConfigMap from that file, with values `configOverrides` merged over it,
   and mounts it at `/build/config/<nf>.json`. A `checksum/config` pod annotation restarts the pod
   when the config changes. `make lab-config` runs the generator, and CI lint fails when a chart
   config is stale (`--check`). The UPF chart's hand-written copy of `config/upf.json` is replaced.
2. **Certificates.** Each chart mounts its own Secret, `<nf>-certs` (values `certs.secretName`),
   at `/build/certs`, with the layout CERTS_DIR expects. The Secret holds only that NF's material:
   `ca.crt`, `cert.pem`, `key.pem` and `nrf-jwt-public.pem`. The NRF's Secret also holds
   `nrf-jwt-private.pem`. Only the files the NF's code opens are mounted; this was checked per NF
   by grepping the paths it reads. `scripts/helm/lab-pki-secrets.sh` writes the 12 Secret
   manifests from the lab PKI (`scripts/gen-lab-pki.sh`).
3. **Service name = NF name.** Each Service is named `<nf>` (values `service.name`), not
   `<release>-<nf>`. The peer URLs in the config and the certificate's `DNS:<nf>` both use that
   name. The Deployment keeps its release-prefixed name.
4. **Found by running the charts, fixed here:**
   - *Kubernetes service links.* Kubernetes injects `<SERVICE>_PORT=tcp://...` into every pod for
     every Service in the namespace. These are the same names as nf_config's `<SERVICE>_<KEY>`
     overrides: AANF, SEPP and TSCTSF crashed parsing `AANF_PORT=tcp://10.96...` as JSON. A
     collision that happened to parse would have changed an NF's config silently. Every chart now
     sets `enableServiceLinks: false`.
   - *The NRF image's entrypoint* runs `gen-lab-pki.sh` before the binary. That script writes a CA
     into `/build/certs` (compose's pki-init volume) and fails on the read-only Secret. The NRF
     chart starts `/build/nrf` directly.
   - *The UPF chart's explicit `appArmorProfile: RuntimeDefault`* makes the kubelet reject the
     pod on a node without AppArmor (kind here). The ReplicaSet then recreated the pod in a loop,
     hundreds of times in a minute. The field is now left unset; on an AppArmor node the runtime
     applies its default profile anyway. The `Localhost` option for ADR-0466's profile is kept.

**Verified (2026-10-23, kind v0.25.0 / Kubernetes v1.31.2 on this host, helm v3.16.4).**
- `helm lint` passes for all 12 charts. `helm template` shows the config mount and the certs
  mount in every chart (the DEP-01 "done when", first half).
- Images: the lab images built on 2026-10-22 (ADR-0505, `docker-<nf>:latest`, PR #21 code). The
  Secrets came from `lab-pki-secrets.sh`. Throwaway Valkey, `postgres-udr` and `postgres-li` pods
  (compose's images, users and schema files) were run for the test only and are not part of this
  change.
- All 12 pods `Running`. Each NF logs that it is listening (the SEPP logs "serving PLMN"). This
  is the DEP-01 "done when", second half.
- 10 of the 10 SBI NFs that register (all except the NRF itself and LI-ADMF) logged "registered
  with NRF (HTTP 201)". Each logged one "no access token" warning first: the NRF issues tokens
  only to registered NFs.
- The SMF discovered the UPF at its pod IP through Nnrf_NFDiscovery and established the PFCP
  association, so the UPF's `auto` address works in a pod.

**Rejected alternatives.**
- *One Secret with the whole lab PKI, mounted into every NF* (what compose does with its
  `certs_data` volume). Every NF would hold every NF's private key and the NRF's token-signing
  key.
- *cert-manager in the charts now.* It adds a cluster dependency to the lab path. The Secret keys
  are documented, so a cert-manager Certificate (or any CA) can fill the same Secrets later.
- *A Helm library chart for the shared templates.* Every install would need `helm dependency
  build` first. The 12 charts carry the same small blocks, written by one script.
- *Config written by hand in each chart's `values.yaml`* (the old UPF chart). It drifts from
  `config/`; the generated file with `--check` cannot.
- *An initContainer that substitutes the pod IP into the config file.* The existing
  `AMF_ADVERTISED_IPV4` override already does this with no extra container.

**Disclosed.**
- **No datastores.** The charts provision no Valkey (AAnF, AUSF, AMF), PostgreSQL (UDR, LI-ADMF)
  or Kafka/Doris. They are named in the configs as `valkey`, `postgres-udr` and so on, and must
  exist as Services with those names. Charts for them, and for the 17 NFs without one, are
  DEP-03.
- **LI X1 is not wired in a cluster.** LI-ADMF's X1 keepalives to the POIs time out, because the
  NF Services do not expose the POIs' X1 ports.
- **The UPF datapath does not start** (`/dev/net/tun` missing in the pod). This is ADR-0466's
  known gap; PFCP and SBI work.
- **Not covered:** `keycloak` and `oam-gui-bff`, which are not NF charts; their descriptions
  still say "no PKI".
- **Helm is not run in CI** (no `helm lint`/`template` job). CI checks only the generated config
  files.
- **Chart defaults vs lab images.** Chart defaults name images `5gc-<nf>:latest`; the lab builds
  `docker-<nf>:latest`. The test set `image.repository`.
- **New finding, outside DEP-01.** 24 NFs hard-code `"ipv4Addresses": ["127.0.0.1"]` in the
  NFProfile they register (only AMF, UPF and NWDAF use a configured address). Any consumer that
  discovers them through the NRF gets an unreachable address, in compose as well as in a
  cluster. It is recorded in the REVIEW-5ae0ef1 status box. The fix waits for PR #27, which
  edits the same 24 `main.cpp` files.
- **README not updated in this PR.** This branch sits on PR #21, whose README predates the
  ADR-0511 restore. The README status row for lab packaging is appended once this branch is
  rebased onto `main` (CLAUDE.md guardrail 10).
