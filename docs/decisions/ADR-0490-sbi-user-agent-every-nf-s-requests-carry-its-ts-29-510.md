## ADR-0490: SBI User-Agent -- every NF's requests carry its TS 29.510 NFType (TS 29.500 5.2.2.2)

**Date:** 2026-10-14. **Status:** Accepted -- built.

**Context.** TS 29.500 table 5.2.2.2-1: User-Agent "shall be mainly used to identify the NF type of
the HTTP/2 client. This header should be included in every HTTP/2 request sent over any SBI"; its
value starts with the TS 29.510 NFType (NOTE 1), optionally followed by "-" and anything else. An
SCP forwarding a request keeps the original value; a request the SCP originates (e.g. to the NRF)
carries "SCP". `libs/sbi-core`'s client never set it. Found while building the NWDAF IRI-POI
(ADR-0489), whose mandatory nWDAFConsumerNFType comes from this header -- without it, a subscription
from this repo's own DCCF produced no record.

**Decision.**
1. `nf_config::load(service_name, ...)` -- called once by every NF at startup -- sets the
   process-wide `SBI_USER_AGENT` to the service's NFType when the service IS an NF: the upper-cased
   name when it is an NFType value of TS29510_Nnrf_NFManagement.yaml (ADRF, AMF, AUSF, BSF, CHF,
   DCCF, GMLC, LMF, MFAF, NEF, NRF, NSACF, NSSF, NWDAF, PCF, SCP, SMF, SMSF, UDM, UDR, UDSF, UPF), and
   `5G_EIR` for `eir`. hello-nf, li-admf / li-mdf / li-mdf3 are not NFs and get none. Same channel as
   the TLS floor (ADR-0478): no NF main is edited; an already-set environment value wins.
2. `sbi_core::http2::Client::send` adds `user-agent: <value>` to a request that does not already
   carry one (case-insensitive), so a caller's own value -- e.g. an SCP relaying a request -- wins.
3. The value is the NFType alone (no "-..." suffix): the spec allows it, and nothing else is needed.

**Disclosed.** The SCP here does not forward requests (it originates its own to the NRF), so "SCP"
is right for all its traffic today; when forwarding is built it must copy the incoming User-Agent,
which (2) already allows. The BSS services do not use `nf_config::load` and send none (not NFs).

**Rejected alternatives.** A per-Client constructor argument: ~30 call sites in every NF to edit,
for a value that is per-process. Deriving the type from the TLS certificate's subject: not what
TS 29.500 names, and the lab certificates are not NF-typed. Sending a generic value from non-NF
services: an invented NF type.

**Verified.** `SbiUserAgent.ConfigLoadSetsTheNfTypeOnlyForAnNf` (amf -> AMF, nwdaf -> NWDAF, eir ->
5G_EIR, hello-nf / li-admf -> none) and `SbiUserAgent.ClientSendsItUnlessTheRequestCarriesOne`
(none when unset; SMF when set; an explicit request header kept).
