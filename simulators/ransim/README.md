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
  in CI runs UERANSIM. A recorded UERANSIM registration and PDU session run against the lab, with
  the exact commands and output, is deferred for future implementation: ADR-0572, v0.2.0 item 5.

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
