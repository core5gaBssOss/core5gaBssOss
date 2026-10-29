## ADR-0532: Lab: listeners bind 0.0.0.0, so a gNB can attach; first UE through the compose lab

**Date:** 2026-10-25. **Status:** Proposed.

**Context.** ADR-0505 left "a UE registration or PDU session through the lab" unverified. Trying it
found why it could not work: the lab's AMF listened for N2 on `127.0.0.5` inside its container
(`config/amf.json` `ngap_bind_address`, a host-test address), so no gNB outside that container could
reach it. `tools/lab/gen_compose_config.py` rewrote loopback URLs and `bind_address` /
`sbi_bind_address`, but no other listener, and its loopback pattern matched only `127.0.0.1` and
`localhost`.

**Decision.**
- The generator rewrites the host of every `*_bind_address` on any `127/8` address (or `localhost`)
  to `0.0.0.0`, keeping the port: in the lab today that is the AMF's `ngap_bind_address`.
- `tests/integration/test_lab_smoke.cpp` (`lab_smoke_tests`, not in ctest): the project's NGAP test
  gNB and NAS UE drive NG Setup, Registration with 5G-AKA and NAS security, RegistrationComplete,
  PDU Session Establishment and Deregistration against the running lab
  (`LAB_AMF_N2=<AMF container IP>`); skipped without it.

**Rejected alternatives.**
- *Publishing 38412/sctp on the host.* Not needed: the host reaches the lab's container IPs on the
  bridge, and publishing SCTP would clash with host-run integration tests on the same port.
- *UERANSIM for the lab run.* It stays the independent cross-check (ADR-0016); the in-tree driver has
  no build step and no config files to keep in sync.

**Verified (2026-10-25, lab from `3ac067b`, AMF restarted with the regenerated config).**
`LAB_AMF_N2=172.30.0.12 lab_smoke_tests`: 1/1 passed -- NG Setup accepted; 5G-AKA (AUTN verified,
RES* accepted by the AUSF); RegistrationAccept with a 5G-GUTI under NAS security; PDU SESSION
ESTABLISHMENT ACCEPT for PDU session 5 / DNN internet; DeregistrationAccept. The NF logs show the
AM policy association at the PCF, the SM context at the SMF, N4 Session Establishment at the UPF,
and at deregistration the N4 Session Release and the CHF Charging Release (ADR-0522). The lab ran
images of `3ac067b`, so no PDU address IE (ADR-0525 is later); the CHF reserved 0 (no rated product
for the lab subscriber). `make lab-registered`: 26 of 26.

**Disclosed.** No user-plane traffic was sent (no GTP-U through the UPF); the lab subscriber has no
balance or rated offering, so no charging amounts were exercised.
