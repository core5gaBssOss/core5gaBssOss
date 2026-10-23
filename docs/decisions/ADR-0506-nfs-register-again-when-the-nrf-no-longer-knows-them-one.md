## ADR-0506: NFs register again when the NRF no longer knows them: one shared NRF heart-beat in sbi-core

**Date:** 2026-10-22. **Status:** Accepted.

**Context.** Bringing the lab up (ADR-0505, PR #21): after the NRF container was replaced, no NF
registered again. The NRF keeps its registry in memory (ADR-0015), so a restarted NRF knows no
instance, and each of the 25 NFs with an NRF lifecycle ran its own copy of the same heartbeat loop,
which only logged "heartbeat failed" every 15 s. Every NF had to be restarted by hand.

TS 29.510 5.2.2.3.2 (29510-j80, step 2b): if the NF instance "is not found in the list of
registered NF Instances in the NRF's database, the NRF shall return "404 Not Found"". The clause
states the 404 but not what the NF does next. The restoration procedures it points to (TS 23.527
clause 6.2) are not available offline and were not read for this.

ADR-0499 adds a second way the same state shows: an NRF that does not know the instance refuses it
a token (`invalid_client`), so once the NF's cached token has expired its heartbeat goes without
one and the NRF's Authorizer answers 401 before the handler could answer 404.

**Decision:** `sbi_core::nrf` (`libs/sbi-core/include/sbi_core/nrf_heartbeat.hpp`):
- `heartbeat()`: the step 1 PATCH ("replace" `/nfStatus` with `REGISTERED`). 200 or 204: accepted.
  404 or 401: NFRegister (5.2.2.2.2) again with the profile the NF registered with, then the NF's
  token back-off is cleared (ADR-0499), as after the first registration.
- `register_instance()`: the NFRegister PUT, with `3gpp-Sbi-Sender-Timestamp` as the NFs send it.
- `run_heartbeats()`: `heartbeat()` every half heart-beat timer, as the NF loops did.
- The 25 NF heartbeat loops (AAnF, ADRF, AMF, AUSF, BSF, CHF, DCCF, 5G-EIR, GMLC, LMF, MFAF, NEF,
  NSACF, NSSF, NWDAF, PCF, SCP, SEPP, SMF, SMSF, TSCTSF, UDM, UDR, UDSF, UPF) are replaced by a
  `run_heartbeats()` call. SEPP keeps its stoppable loop and calls `heartbeat()`; SCP and SEPP pass
  their own `TokenSource`'s `clear_backoff`.

Re-registering on a 401 is safe for an NF the NRF still knows: the NRF admits NFRegister without a
token only for an unknown instance, and replacing a known profile needs a token like any other
NFManagement request. So that PUT is refused too, and nothing is changed.

**Rejected alternatives.**
- *Re-register only on 404.* The NF would stay unregistered for good once its token had expired
  during the NRF outage. Each later heartbeat would go without a token and get 401, never 404.
- *Persist the NRF registry.* This would solve the NRF restart, but not an NRF that lost an
  instance any other way (an operator removing it, a failover to an NRF without it). It is also a
  storage decision ADR-0015 deferred. The NF reaction is needed either way.
- *Patch each NF's loop in place.* That would mean 25 copies of the same rule. The loops were
  already identical copies, which is how all 25 came to share the bug.
- *Also move each NF's first NFRegister loop into sbi-core.* Those loops differ: SEPP can be
  stopped, NWDAF sets a flag, and several log differently. Left for a separate change.

**Verified (2026-10-22, this host, CI running alongside).**
- `sbi_nrf_heartbeat_tests` (5 tests, written for this change, against an in-process NRF stand-in
  answering as `nfs/nrf` does): a known instance is only heartbeaten. After a restart, the 404
  heartbeat registers the same profile again, and the next heartbeat is accepted. A tokenless 401
  for an unknown instance registers again. A 401 for a known instance changes nothing. An
  unreachable NRF registers nothing. 5/5 pass.
- The 25 NF binaries and the NRF compile with the change.
- Real processes (the `nrf`, `eir` and `scp` binaries on ports 17777/17791/17792, off CI's ports):
  both NFs registered, the NRF accepted two heartbeats, then the NRF was killed and started again.
  - With PR #26's NRF: at the next heartbeat (7 s after the restart) both NFs got 404 and registered
    again (201). The new NRF then accepted every heartbeat from both, with no token refused.
  - Without PR #26's NRF: the EIR could never get a token ("does not name the NF type"), so every
    heartbeat was 401 and the PUT for the known instance was refused (401). After the NRF dropped it
    for a missed heartBeatTimer, the next 401 heartbeat registered it again (201). This exercised
    the 401 path against the real NRF.
- Not run: the full ctest suite, nor the lab (CI was using the host).

**Disclosed.**
- Not handled: the 3xx redirection of step 2b (another NRF instance's Location). It is logged as a
  failed heartbeat, as before.
- The NRF's subscriptions (NFStatusSubscribe) are in memory too, and nothing here restores them.
  No NF subscribes to the NRF today (no NF calls `nnrf-nfm/v1/subscriptions`). The first one that
  does needs the same rule.
- The NRF answers a heartbeat with 200 and the profile, never 204. Both are accepted.
- hello-nf (a one-shot demo that registers, heartbeats once and deregisters) is unchanged.
