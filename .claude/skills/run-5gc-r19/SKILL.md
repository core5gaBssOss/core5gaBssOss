---
name: run-5gc-r19
description: Run, start, launch and drive the 5GC network functions (NRF, AMF, SMF, EIR, PCF, UPF, ...) and the operator GUI - bring NFs up as local processes, register a consumer, get OAuth2 tokens, call SBI APIs with curl over mTLS, restart the NRF, screenshot the operator console. Use when asked to run the core, try an NF's API, see a change working in the real binaries, or take a screenshot of the GUI.
---

The NFs are HTTP/2 + mTLS servers that register with the NRF and only accept requests that carry a
real NRF-issued OAuth2 token. Drive them with `.claude/skills/run-5gc-r19/driver.sh`. It starts the
built binaries as local processes on ports shifted by +10000 (NRF on 17777), so they coexist with
CI and the compose lab. It also registers a stand-in consumer NF and makes token-carrying curl
calls for you. Paths are relative to the repo root.

## Prerequisites

The binaries come from `build/`. Building them is its own skill: `5gc-build-and-verify`. A full
build takes about 2 hours; an incremental NF target takes minutes. The driver needs `curl` (with
HTTP/2), `python3`, `docker` (only for `gui-build`) and `google-chrome` (only for `shot`). All of
them are on this host. There is no host Node.

```bash
cmake --build build -j4 --target nrf eir nssf bsf udm pcf smf upf
```

## Run (agent path)

```bash
D=.claude/skills/run-5gc-r19/driver.sh
$D up                       # NRF + eir nssf bsf udm pcf smf; waits for each NF's registration (~1 s)
$D consumer AMF             # registers a stand-in AMF (certs/amf/) with the NRF
$D disc 5G_EIR              # Nnrf_NFDiscovery as that AMF -> {"nfInstances":[{... "nfType":"5G_EIR"}]}
$D call GET eir "/n5g-eir-eic/v1/equipment-status?pei=imeisv-3510000000000020"
                            # -> {"status":"BLACKLISTED"}  HTTP 200
$D token nudr-dr UDR        # -> token refused: {"error":"invalid_scope",...}  exit 1
$D restart nrf              # in-memory registry gone; NFs re-register at their next heartbeat
$D logs nrf 20
$D status
$D down
```

| command | what it does |
|---|---|
| `up [nf...]` | start the NRF and the given NFs (default `eir nssf bsf udm pcf smf`). Configs are generated from `config/<nf>.json` into `/tmp/5gc-run/cfg/` |
| `consumer [TYPE]` | NFRegister a consumer of TYPE using `certs/<type>/`; later calls act as it |
| `token SCOPE TARGET` | print an access token (`POST /oauth2/token`) |
| `disc TARGET_TYPE` | discovery as the consumer |
| `call METHOD NF PATH [JSON]` | request to NF's SBI; the scope is PATH's first segment, the target is NF's type |
| `restart NF` / `logs NF [N]` / `status` / `down` | lifecycle; the previous log of a restarted NF is `/tmp/5gc-run/<nf>.log.prev` |
| `gui-build` / `gui-serve` / `shot URL [NAME]` | operator GUI, below |

State and logs are in `/tmp/5gc-run/` (override with `RUN_DIR`). Override the port shift with
`PORT_OFFSET`.

### Operator GUI (static only)

```bash
$D gui-build                               # npm ci + vite build in node:22-alpine (~20 s)
$D gui-serve                               # http://127.0.0.1:18080/
$D shot http://127.0.0.1:18080/ console    # -> /tmp/5gc-run/shots/console.png
```

The screenshot shows the console's signed-out page ("Sign in ... multi-factor authentication is
required"). That is the most this path can show. The real server, `oam-gui-bff`, needs an
`operator_iam` Postgres schema before it listens, a browser with an operator-CA client certificate,
and Keycloak with MFA. None of that is set up here.

## Run (whole lab, Docker Compose)

```bash
make lab-build-fast   # one shared builder compiles everything once (ADR-0505); measured ~35 min warm
make lab-up
make lab-registered   # every NF's log shows its NRF registration; expect 26/26
make lab-down
```

The lab uses the real ports (7777, 9464, ...), the same ones as the CI runner on this host. Stop the
runner first, or `lab-up` and CI collide.

## Test

```bash
./build/tests/integration/nrf_discovery_authz_tests     # 15 tests, 2 ms, no ports
```

Full ctest, and its traps: skill `5gc-build-and-verify`.

## Gotchas

- **Datastore ports are never shifted.** Shifted by 10000, Postgres 5434/5437 would land on CI's own
  service containers (15434..15439). The driver leaves `postgresql://` and `tcp://` URLs and
  `data_store`/`redis_*`/`doris_*` keys alone. Datastore-backed NFs (udr, amf, ausf, chf, nwdaf,
  ...) therefore reach the host's local datastores on their real ports, or fail to start. The
  default set needs none.
- **A call can fail one hop away.** For example,
  `call GET udm /nudm-sdm/v2/imsi-001010000000001/am-data` answers 500 with "UDM could not obtain a
  token for UDR". With no UDR registered, the NRF refuses the UDM a token (`invalid_scope`).
- **The consumer is a real registration.** Tokens are bound to it: the NRF checks the instance is
  registered and that the client certificate names its NF type (ADR-0499). After `restart nrf`, run
  `consumer` again, or every token is refused with `invalid_client`.
- **The binaries are whatever `build/` last compiled, not the checked-out source.** After switching
  branches, rebuild the targets before trusting what you see. `restart nrf` only re-registers the NFs
  in a build that includes ADR-0506 (PR #27). Older binaries log "heartbeat failed" forever.
- **The UPF runs without its datapath.** As a local process it cannot get
  `CAP_NET_ADMIN`/`CAP_SYS_ADMIN`, so XDP is skipped ("eBPF/XDP datapath not started"). PFCP and SBI
  still work: `up upf` makes the SMF log "PFCP Sx Association established". Without a UPF the SMF
  logs "no UPF registered with NRF yet" every 2 s; that is harmless.
- **`status` says "registered" if the NF's log ever showed a registration.** After `restart nrf`,
  check `$D logs nrf` for "registered new NF instance" lines instead.
- **"no access token for nnrf-nfm ... invalid_client" once at each NF start is normal.** An NF asks
  for its first token before it is registered.

## Troubleshooting

- **`node: command not found`** when building `gui/web`: the host has no Node. Use `$D gui-build`,
  which runs `npm ci && npm run build` in `node:22-alpine` as your UID.
- **`token refused: {"error":"invalid_client","error_description":"the requesting NF instance is not
  registered"}`** after `restart nrf`: the consumer died with the NRF's registry. Run `$D consumer
  AMF` again.
- **`driver: <nf> exited; tail /tmp/5gc-run/<nf>.log`**: read that log. A FATAL from `nf_config`
  names the missing key or an unreachable datastore.
