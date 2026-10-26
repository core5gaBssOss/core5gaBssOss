## ADR-0525: SMF allocates the UE IPv4 address from a per-DNN pool (TS 23.501 5.8.2.2.1)

**Date:** 2026-10-24. **Status:** Proposed.

**Context.** The SMF never allocated a UE IP address (disclosed since the first SMF ADRs): the PDU
SESSION ESTABLISHMENT ACCEPT carried no PDU address, the UPF's uplink PDR matched no UE address,
the PCF's SM policy had no `ipv4Address`, and ADR-0523's step 2 ("releases the IP address") had
nothing to release. It also blocks the PCF's session binding (TS 29.513) that AF events need -- the
next increment. User direction 2026-10-24: strictly per 3GPP.

**Decision.**
- `smf::UeIpPool` (`nfs/smf/src/ue_ip_pool.{hpp,cpp}`): one IPv4 pool per DNN from
  `config/smf.json` `ue_ipv4_pools` (CIDR, /8../30; "internet": 10.45.0.0/16). The lowest free host
  address is allocated; network and broadcast addresses never are. A malformed pool is a startup
  configuration error.
- CreateSMContext allocates before anything else is set up for the session (TS 23.501 5.8.2.2.1,
  SMF-allocated). None free, or no pool for the DNN: PDU SESSION ESTABLISHMENT REJECT, 5GSM cause
  #26 "insufficient resources" (TS 24.501 6.4.1.4.1), in SmContextCreateError like the NSACF refusal
  (ADR-0279). A guard returns the address on every failure path after allocation.
- The address goes into: the Accept's PDU address IE (TS 24.501 8.3.2.1.1 / 9.11.4.10: IEI 0x29,
  TLV, type IPv4), the uplink PDR's PDI as a UE IP Address IE with S/D = 0, source (TS 29.244
  8.2.62), the SM policy's `SmPolicyContextData.ipv4Address`, and the SM context (`ueIpv4`, `dnn`).
- PDU session release (every trigger, ADR-0523) returns it to the pool (TS 23.502 4.3.4.2 step 2).

**Rejected alternatives.**
- *UPF-allocated addresses (CHV4).* TS 23.501 allows either; SMF allocation keeps the address known
  before N4 and lets the PCF bind on it at SM policy creation.
- *One global pool.* Pools are per DNN in TS 23.501 (the address belongs to the DN).

**Disclosed.**
- IPv4 only: no IPv6 prefix / interface identifier, no IPv4v6; the PDU session type is still the
  fixed IPv4 the SMF already assumed.
- In memory: an SMF restart forgets which addresses are in use (as it forgets the SM contexts).
- No DHCP, no static (subscribed) addresses from UDM, no UPF-side IP pools, no IP pool identity.
- No downlink PDR with the UE address as destination (the SMF has no downlink path at N4 yet).
- Allocation scans the in-use set (linear in the number of sessions of the DNN).

**Verified (2026-10-24).** See below.
