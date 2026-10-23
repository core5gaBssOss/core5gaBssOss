## ADR-0518: NFs register their advertised address in the NFProfile, not 127.0.0.1

**Date:** 2026-10-23. **Status:** Proposed.

**Context.** Disclosed in ADR-0513 and in the REVIEW-5ae0ef1 status box: 23 NFs registered
`ipv4Addresses: ["127.0.0.1"]` in their NFProfile (TS 29.510) whatever their config said. Only the
UPF (ADR-0466) and the NWDAF registered a configured address. The AMF, ADRF, DCCF, MFAF, NSACF and
UDSF already read `advertised_ipv4` -- the AMF even passed it to its NRF thread -- but used it only
for their own URLs. A consumer that discovers one of these NFs through the NRF and connects to the
registered address reaches its own loopback in any container or pod. The UDSF also registered
`nfServices[].ipEndPoints[].ipv4Address` as 127.0.0.1. The 127.0.0.1 placeholder had a reason: the
NRF rejects a hostname there (400 "ipv4Addresses contains an invalid IPv4 address", ADR-0453), and
`advertised_ipv4` was sometimes a compose service name.

**Decision:**
- `libs/sbi-core` gets the UPF's ADR-0466 resolver, moved from `nfs/upf/src`
  (`sbi_core::resolve_advertised_ipv4`, `ipv4_to_string`), plus `advertised_ipv4_or_die`, which
  calls `nf_config::fatal` when there is no address. A setting is a literal dotted-quad IPv4 (not
  0.0.0.0) or `"auto"`, the local address toward the NRF. A hostname is refused, as the NRF refuses
  one.
- All 23 NFs (AAnF, ADRF, AMF, AUSF, BSF, CHF, DCCF, EIR, GMLC, LMF, MFAF, NEF, NSACF, NSSF, PCF,
  SCP, SEPP, SMF, SMSF, TSCTSF, UDM, UDR, UDSF) resolve `advertised_ipv4`
  (`<NF>_ADVERTISED_IPV4` first) once at startup, before the NRF thread starts, and register that
  address. The UDSF uses it in `ipEndPoints` too. An NF whose setting cannot be resolved exits
  instead of registering a guess.
- The six NFs that already read `advertised_ipv4` keep using the raw value for their own URLs
  (`self_base` and the like); only the NFProfile gets the resolved literal.
- The 17 NFs whose config did not have the key get `"advertised_ipv4": "127.0.0.1"` in
  `config/<nf>.json`, after `sbi_bind_address`: on a single host that is the right address and
  what they registered before. The GUI config schemas are regenerated
  (`gui/schema-gen/derive_nf_config_schemas.py`).

**Rejected alternatives.**
- *Default to `"auto"` in `config/`.* Correct everywhere for these 17, but config/ is the host-run
  config, and every other NF's host value is a literal; the lab and Helm generators set the
  environment-specific value (below).
- *Resolve a hostname to an address, as the NWDAF does.* Accepts a name the NRF itself rejects and
  hides a DNS dependency in startup; the UPF's rule (literal or `"auto"`) is the stricter one. The
  NWDAF keeps its own resolver for now; moving it to the shared one would refuse names it accepts
  today.
- *Register `fqdn` instead of an address.* TS 29.510 allows either, but consumers in this project
  read `ipv4Addresses` (the SMF for the UPF, the MTLF for the AnLF), and the lab certificates name
  services, not FQDNs.

**Disclosed.**
- **Built on PR #27** (`fix/nrf-reregistration`), whose NRF lifecycle changes touch the same
  functions. #27 is off `main`, which does not yet have the lab and Helm generators (they arrive
  with #21, ADR-0503, and #42, ADR-0513). So the deployment half is **not in this change**:
  `tools/lab/gen_compose_config.py` already rewrites a loopback `advertised_ipv4` to the container
  IP, so the compose lab needs only `make lab-config`; but `tools/lab/gen_helm_config.py` refuses
  an `advertised_ipv4` it has no rule for, so whichever of this branch and that stack reaches
  `main` second must add the 17 NFs to its `ADVERTISED_AUTO` set (they use the address only for
  the NFProfile, so `"auto"`, the pod IP, fits) and regenerate both config sets.
- `nfs/hello-nf` still registers 127.0.0.1: it is a test client that registers and deregisters at
  once, not a deployed NF.
- No NF's discovery consumer was changed: SBI calls in this project still use configured base
  URLs, so this fixes what the NRF returns, not how NFs choose peers.

**Verified (2026-10-23).**
- `tests/integration/test_nf_profile_address.cpp` (`NfProfileAddress.*`, 3 tests): the real EIR
  binary registers with an in-process NRF stand-in. `EIR_ADVERTISED_IPV4=192.0.2.10` -> the
  NFProfile carries `["192.0.2.10"]`; `"auto"` -> `["127.0.0.1"]` (the stand-in is on loopback); a
  hostname -> the EIR exits non-zero and registers nothing. 3/3 pass. Against the EIR built from
  #27's `main.cpp` (before this change) the first and third fail -- it registers `["127.0.0.1"]`
  and keeps running with a hostname; the `"auto"` case passes there by coincidence (loopback is
  the right answer for that set-up).
- `upf_advertised_address_tests` (the moved resolver's 8 tests, now against sbi-core): 8/8 pass.
- Built: `eir`, `upf` and both test binaries (`ninja`, 13 steps, no failures). The other 22 changed
  `main.cpp` files and the new sbi-core source were checked with `-fsyntax-only` against the
  compile database (all pass after one fix: two remaining `upf::Ipv4` uses in `nfs/upf`). They were
  not linked or run here, and the full ctest suite was not run.
- `clang-format-18 --dry-run -Werror` clean on every changed C++ file; `check_adr_index.py` OK.
