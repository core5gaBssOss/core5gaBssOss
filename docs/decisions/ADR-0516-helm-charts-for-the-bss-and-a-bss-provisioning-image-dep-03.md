## ADR-0516: Helm charts for the BSS and a bss/provisioning image (DEP-03, BSS half)

**Date:** 2026-10-23. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 DEP-03: CLAUDE.md definition of done item 7 (Docker image + Compose
entry + Helm chart) was unmet for all of `bss/`. ADR-0515 (PR #44) gave the 17 NFs without a chart
one in the ADR-0513 shape and left the BSS for this increment. Four BSS components
(`product-catalog`, `balance-management`, `subscriber-management`, `roaming-interconnect`) had an
image and a compose service but no chart; `bss/provisioning` (the ADR-0382 customer
order-to-activation runner) had none of the three, although the oam-gui-bff's compose config
already names `https://provisioning:7802` (ADR-0442).

**Decision:**
- `deploy/docker/provisioning.Dockerfile`, the balance-management Dockerfile with
  `--target provisioning`; the ADR-0505 shared builder picks the target up from it. A compose
  service `provisioning` (pki-init, `postgres-chf` healthy; both domain DBs by environment, as its
  siblings set theirs; the ADR-0453 `config:ro` mount), and `provisioning` in the pki-init list so
  `certs/provisioning/` exists -- the identity the UDR's OAM provisioning API admits
  (`config/udr.json` `oam_provisioning_allowed_clients`). Its UDR address comes from the
  generated lab config (`https://udr:7781`): `udr_base_url` has no environment override.
- **Port fix:** `config/provisioning.json` metrics moves 9492 -> **9498**. ADR-0391 gave
  provisioning 9492; the LI-ADMF later took the same port (`config/li-admf.json`), so the two
  could not run on one host or both publish it in compose. Provisioning moves because nothing
  deployed referenced its metrics port; 9498 is used by no config, compose file, chart or test.
- Five charts in the ADR-0513/0515 shape: generated config ConfigMap at `/build/config`, the
  component's certs Secret at `/build/certs` (CA, its certificate and key; no BSS component opens
  `nrf-jwt/`), Service named after the component with its API and metrics ports, one replica,
  `enableServiceLinks: false`. Port name `api`, not `sbi`: these are TM Forum / OAM APIs.
- `tools/lab/gen_helm_config.py`: the product catalog builds its resource hrefs from
  `advertised_ipv4` and reads `PRODUCT_CATALOG_ADVERTISED_IPV4` first, so its chart sets that from
  the pod IP (the ADR-0515 table, which now maps `-` to `_` in the variable name).
  `config/provisioning.json` has an `advertised_ipv4` no source line reads (the GUI schema's
  review note says the same); the chart config drops it (`ADVERTISED_UNREAD`) instead of inventing
  a value. The other three set `self_base_url` to their Service name already.
- The compose lab config set is regenerated: `provisioning` takes a fixed IP, which shifts the
  alphabetically later services by one; of those, only the UDSF's `advertised_ipv4` changes.

**Rejected alternatives.**
- *Removing `advertised_ipv4` from `config/provisioning.json`.* Correct, but it changes the
  host-run config and the GUI config schema for a deployment increment; left for a config cleanup.
- *Moving the LI-ADMF off 9492 instead.* It already ships in compose and in a chart
  (`deploy/helm/li-admf`); provisioning shipped nowhere.
- *Making `udr_base_url` an environment-overridable key now.* Not needed for the lab (the
  generated config set rewrites it) or Helm (the chart config is generated); a code change in a
  deployment increment.
- *Product catalog hrefs on its Service name instead of the pod IP.* The key is an IPv4 address by
  name and code; using a DNS name there would be a semantic change to the component, not a chart
  setting.

**Disclosed.**
- **Not run in a cluster** (no kind/kubectl/helm on the host; ADR-0515's limit). Verified only by
  `helm lint` and `helm template`.
- **Datastores are not provisioned** by the charts (as ADR-0513/0515): all five need PostgreSQL
  (`charging`; provisioning also `orchestration`) and exit at startup without it; provisioning
  needs the UDR for an order to complete.
- **The product catalog's hrefs name a pod IP**, which is not in its certificate (DNS SANs only),
  so a client following one with hostname verification fails -- the same as in compose today,
  where they name the container IP.
- **Provisioning was not driven end to end**: no customer order was placed (that needs the UDR and
  its database up). The image was started in compose and answered one GET (below).
- The provisioning image in the run below was built from the ADR-0505 shared builder image (tree
  of 2026-10-22), incrementally for this target, not by a cold `make lab-build`.
- Provisioning's 404 body is `{"error": ...}`, not ProblemDetails -- pre-existing; it is an OAM
  API, not an SBI service, but noted.
- One replica each; more than one not verified.

**Verified (2026-10-23).**
- `alpine/helm:3.16.2`: `helm lint` passes and `helm template` renders for all five charts: a
  ConfigMap (data parses as JSON, no `advertised_ipv4`), a Deployment (secret items CA, cert,
  key; product catalog with `PRODUCT_CATALOG_ADVERTISED_IPV4` from `status.podIP`) and a Service
  named after the component with its two ports.
- `docker buildx build -f deploy/docker/provisioning.Dockerfile` with the shared builder context,
  then `docker compose ... up provisioning` (lab overlay): pki-init wrote the new certificate;
  log "connected to orchestration + charging PostgreSQL" and "listening on https://0.0.0.0:7802";
  from the host, an mTLS HTTP/2 GET of an unknown order -> 404 (a real orchestration-DB lookup);
  `/metrics` on 9498 -> 200 with the `provisioning_*` gauges; no client certificate -> TLS refused.
- `gen_compose_config.py --check` and `gen_helm_config.py --check` (34 chart configs) pass.
