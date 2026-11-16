# ransim -- UERANSIM-based RAN/UE simulator

Arms-length external test tool, not part of this project's own source tree. See
`docs/DECISIONS.md` ADR-0016 for the full rationale (license, why it's fetched instead of
vendored, why it's a separate directory, current scope/deferred items).

## What this is

[UERANSIM](https://github.com/aligungr/UERANSIM) (tag `v3.3.0`, commit
`6bf5a1a96aaef6ae8778b9d8b477ac6e2bbf8156`), an open-source, 3GPP-Release-15-conformant 5G-SA UE
and gNodeB simulator: real NGAP (N2) and NAS (N1) control-plane messages, real GTP-U (N3) user
plane. The NR radio interface itself is simulated over UDP loopback, not real RF.

**License: AGPL-3.0** (dual-licensed with a commercial option by its author). We run it
**unmodified**, as its own separate process, talking standard protocols (NGAP/SCTP, NAS, GTP-U)
to our NFs over the wire -- never linked into any binary in `libs/` or `nfs/`. Do not vendor its
source into this repository, do not copy code from it into `libs/`/`nfs/`, and do not modify the
checked-out copy in `vendor/` and redistribute that modified copy -- any of those would change the
license analysis in ADR-0016.

## Layout

- `fetch-and-build.sh` -- clones the pinned commit into `vendor/UERANSIM/` (gitignored, not
  committed) and builds it there.
- `vendor/` -- gitignored. Created by `fetch-and-build.sh`. Not part of this repository's git
  history.
- `config/gnb.yaml`, `config/ue.yaml` -- the lab config `nr-gnb`/`nr-ue` need. Both are in the
  repository (an earlier version of this section said they were not; corrected 2026-11-10,
  ADR-0572 v0.2.0 item 1). What they point at, checked against the repo on 2026-11-10 by reading,
  not by running:
  - `gnb.yaml` `amfConfigs` = `127.0.0.5:38412`, the AMF's `ngap_bind_address`/`ngap_bind_port`
    in `config/amf.json`; PLMN 999/70, TAC 1, S-NSSAI sst 1 / sd 1.
  - `ue.yaml` SUPI `imsi-999700000000001` with the TS 35.207 Test Set 1 K/OP/AMF, the subscriber
    the UDR seeds (`nfs/udr/src/main.cpp`); one `IPv4` session on DNN `internet`, which is the
    SMF's `ue_ipv4_pools` entry in `config/smf.json`.
- **What drives the UE in this project's tests.** The integration tests do not use UERANSIM; they
  use the project's own NAS/NGAP driver, `tests/integration/ue_nas_driver.cpp`, so registration,
  PDU session establishment and N2/Xn handover are proven against the project's own codec. Nothing
  in CI runs UERANSIM. A recorded UERANSIM registration and PDU session run against the core is
  below, under "Recorded run" (ADR-0572, v0.2.0 item 5); it was done once by hand, not in CI.

## Usage

```
./fetch-and-build.sh
cd vendor/UERANSIM/build
./nr-gnb -c ../../../config/gnb.yaml
./nr-ue  -c ../../../config/ue.yaml   # separate terminal
```

## Prerequisites

`cmake`, a C++17 compiler, and `libsctp-dev` (Ubuntu/Debian package name; UERANSIM's NGAP
transport is real SCTP, not simulated).

## Recorded run (2026-11-16)

One manual run of UERANSIM v3.3.0 (the pinned commit above, built unmodified by
`fetch-and-build.sh`) against the core's NFs as local processes. It shows that a third-party gNB and
UE complete NG Setup, 5G-AKA registration and a PDU session establishment against this core's
AMF/AUSF/UDM/UDR/SMF/PCF. It does **not** show user-plane traffic (see "Not shown" below).

What ran: `nrf udr udm ausf pcf nssf smf upf amf` from `build/` (binaries last compiled
2026-11-15; the exact commit they were built from was not recorded, so this is not tied to a git
SHA), started with `.claude/skills/run-5gc-r19/driver.sh up nrf udr udm ausf pcf nssf smf upf amf`.
SBI ports are shifted by +10000 by that driver; the AMF's NGAP listener (`127.0.0.5:38412`) is not,
which is what `config/gnb.yaml` points at. Datastores: Postgres for the UDR on 5437 and Valkey on
6379 (local containers). Run from `simulators/ransim/`:

```
vendor/UERANSIM/build/nr-gnb -c config/gnb.yaml
vendor/UERANSIM/build/nr-ue  -c config/ue.yaml        # as a normal user, not sudo
vendor/UERANSIM/build/nr-cli imsi-999700000000001 -e ps-list
```

`nr-gnb` (excerpt, full lines kept):

```
[2026-11-16 12:36:16.356] [sctp] [info] SCTP connection established (127.0.0.5:38412)
[2026-11-16 12:36:16.357] [ngap] [debug] Sending NG Setup Request
[2026-11-16 12:36:16.360] [ngap] [debug] NG Setup Response received
[2026-11-16 12:36:16.360] [ngap] [info] NG Setup procedure is successful
```

`nr-ue` (filtered to the NAS procedure lines; timestamps are local time):

```
[2026-11-16 12:36:26.796] [nas] [debug] Sending Initial Registration
[2026-11-16 12:36:26.858] [nas] [debug] Authentication Request received
[2026-11-16 12:36:26.858] [nas] [debug] Sending Authentication Failure due to SQN out of range
[2026-11-16 12:36:26.869] [nas] [debug] Authentication Request received
[2026-11-16 12:36:26.872] [nas] [debug] Security Mode Command received
[2026-11-16 12:36:26.878] [nas] [debug] Registration accept received
[2026-11-16 12:36:26.878] [nas] [info] Initial Registration is successful
[2026-11-16 12:36:26.878] [nas] [debug] Sending PDU Session Establishment Request
[2026-11-16 12:36:27.166] [nas] [debug] PDU Session Establishment Accept received
[2026-11-16 12:36:27.166] [nas] [info] PDU Session establishment is successful PSI[1]
[2026-11-16 12:36:27.166] [app] [error] TUN interface could not be setup. Permission denied. Please run the UE with 'sudo'
```

`nr-cli ... -e ps-list` after that:

```
PDU Session1:
 state: PS-ACTIVE
 session-type: IPv4
 apn: internet
 s-nssai:
  sst: 0x01
  sd: 0x000001
 emergency: false
 address: 10.45.0.1
 ambr: up[1Gb/s] down[1Gb/s]
 data-pending: false
```

`10.45.0.1` is from the SMF's `internet` pool. The AMF log shows the matching steps (SQN
resynchronisation, RegistrationAccept, RegistrationComplete verified, AM Policy Association with
the PCF, SM context established with the SMF), and the SMF logs "PDU Session Establishment Accept
delivered to AMF".

Observations:

- The first Authentication Request carried an SQN the UE rejected (`Received SQN [FF9BB4D0B607]`,
  `SQN-MS [000000000000]`, "Sending Authentication Failure due to SQN out of range"). The AMF
  answered with its SQN resynchronisation retry and the second challenge succeeded. This is the
  AMF's handling of an AUTS failure working as designed, not an error. Why the first challenge's
  SQN was out of range was not investigated (not looked into: the seeded SQN versus the UE's
  configured one).
- Registration took about 82 ms and PDU session establishment about 288 ms in this run. Single
  run, loopback, debug logging: not a performance measurement.

Not shown, a gap:

- **No user plane.** `nr-ue` logged `TUN interface could not be setup. Permission denied. Please run
  the UE with 'sudo'`, so no `uesimtun0`, no ping or traffic through the UPF. As a local process the
  UPF also has no XDP datapath (`run-5gc-r19` skill), so N3 forwarding is not exercised either way.
  Establishing the PDU session (N1/N2/N11/N4 signalling) is what this run proves; data transfer is
  not proven by it.
- One UE, one run, IPv4 only, no deregistration, handover, or paging.
