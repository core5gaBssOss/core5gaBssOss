## ADR-0503: make lab-up: a generated compose config set with fixed container IPs

**Date:** 2026-10-20. **Status:** Accepted.

**Context.** REVIEW-5ae0ef1 DEP-04: no Makefile, so no `make lab-up` (CLAUDE.md: "all
reproducible from a single `make lab-up`"). DEP-02: `config/<nf>.json` is written for one host --
the NRF, every peer and every datastore are `127.0.0.1` -- and in compose 127.0.0.1 is the
container itself. Only AMF, UPF, UDR, UDSF and CHF had environment overrides; the AAnF and AUSF
exited at startup (Valkey at 127.0.0.1:6379), 19 NFs could never reach the NRF. Also found here:
`nwdaf-mtlf` published host ports 7797 and 9486, already the NSACF's and the NSSF's, so
`docker compose up` could not bind them.

**Decision (user decision 2026-10-20: "Compose config set").**

1. `tools/lab/gen_compose_config.py` generates, from `config/` (still the single source),
   `deploy/docker/lab/config/<service>.json` and `deploy/docker/docker-compose.lab.yml`. Rewrites:
   a loopback `host:port` becomes the compose service publishing that port (or the NF whose
   `x1_port` it is); datastores map to their containers (`valkey:6379`, `postgres-chf:5432`,
   `postgres-udr`, `postgres-adrf`, `postgres-li`, `kafka-1:9092`, `doris`); `*_host` keys to
   `doris` / `li-mdf` / `li-mdf3`; `bind_address` to `0.0.0.0`; `*_sni` stays `localhost` (every
   lab certificate carries `DNS:localhost` and `DNS:<service>`, `scripts/gen-lab-pki.sh:102`). A
   `mtlf_*` key on 7797 goes to `nwdaf-mtlf`, any other 7797 to `nsacf`.
2. `advertised_ipv4` -- the address an NF registers in the NRF, which must be an IP -- is the
   container's fixed address on the `lab` network (172.30.0.0/24, one address per service, sorted
   by name). The override puts every service on that network.
3. Each NF is pointed at its file by the existing `<SERVICE>_CONFIG_FILE` (nf_config.hpp, since
   2026-08-16, so the 7-day-old images honour it too), mounted at
   `/build/deploy/docker/lab/config`. The ADR-0453 `../../config:/build/config:ro` mount stays.
   Services that already name a hand-written compose config (LI-ADMF, OAM GUI BFF) keep it.
   `nwdaf-mtlf` gets its own copy of `nwdaf.json` (its own advertised IP).
4. AAnF and AUSF `depends_on: valkey`. `nwdaf-mtlf` is published on host ports 7799/9488.
5. `Makefile`: `lab-up` (generate, `docker compose up -d`, status), `lab-build`, `lab-status`
   (lists services that exited with an error), `lab-logs`, `lab-down`, `lab-clean`, `lab-config`.
6. CI lint: `gen_compose_config.py --check` fails when `config/` changed without `make lab-config`.

**Disclosed.**
- Images: each `deploy/docker/<nf>.Dockerfile` compiles the whole repository in its own builder
  stage, so `make lab-build` of ~35 images is many hours. `lab-up` builds only missing images and
  otherwise runs the existing ones (on this host: 7 days old, so without ADR-0499..0502). A shared
  builder stage is the next step.
- Not covered: `provisioning` and `mcp-server` have no compose service; the SEPP's peer-PLMN
  `n32_api_root` (127.0.0.1:7823) is a remote SEPP, left as is.
- `lab-status` reports container state, not "registered with NRF" (the DEP-02 "done when"); that
  needs an authenticated NFDiscover against the NRF.
- The lab publishes the same host ports the CI tests use; it must not run while the self-hosted CI
  runner (this host) runs integration tests.

**Rejected alternatives.**
- *Per-service environment overrides* (the ADR-0077 retrofit pattern): several peer URLs have no
  override in the code (e.g. NEF -> UDR/PCF/UDM/AAnF), so it needed C++ changes in ~10 NFs and
  image rebuilds first.
- *Mounting a compose config directory over `/build/config`*: replaces the ADR-0453 mount the
  definition of done mandates.
- *Hostnames in `advertised_ipv4`*: NFProfile `ipv4Addresses` are addresses; an NF found by
  NFDiscover would be unreachable.
- *A hand-written compose config set*: drifts from `config/` silently; generated plus a CI check.

**Verified (2026-10-20, this host, existing 7-day-old images).** `make lab-up` run three times.
First run found four defects, fixed in this ADR: `doris-schema-init` ran before a Doris backend
registered (now waits for `SHOW BACKENDS` Alive); the NWDAF started without the `chf_features`
database (now depends on the init); the SMF image lacked `libli_core.so` (six Dockerfiles --
SMF, CHF, NEF, NWDAF, SMSF, UDM -- now ship li_core and its schemas, as AMF/UPF did); and a second
run failed at `valkey-cluster-init` (now skips a formed cluster). Third run: every service running
except the SMF (its image predates the Dockerfile fix) and AAnF, SEPP, TSCTSF (no image built yet);
22 NFs logged "registered with NRF (HTTP 201)" -- NRF, AMF, UPF, UDM, UDR, AUSF, PCF, NSSF, BSF,
NEF, SCP, EIR, NSACF, NWDAF (AnLF + MTLF), MFAF, UDSF, DCCF, ADRF, SMSF, GMLC, LMF, CHF -- with
heartbeats continuing. Not verified: a UE registration or PDU session through the lab; the UPF's
eBPF/XDP datapath does not start in its unprivileged container (PFCP signalling works).
