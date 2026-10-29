## ADR-0526: PCF session binding of AF sessions and termination on PDU session end (TS 29.513, TS 29.514 4.2.2.2, 4.2.5.3)

**Date:** 2026-10-24. **Status:** Proposed.

**Context.** The PCF accepted any Npcf_PolicyAuthorization_Create without relating it to a PDU
session, so it could never tell the AF anything about that session (ADR-0519/0520 disclosed "the
PCF emits no events"). With UE IPv4 allocation (ADR-0525) the SM policy now carries the UE address,
which is what session binding uses. User direction 2026-10-24: strictly per 3GPP.

**Decision.**
- **Binding** (TS 29.514 4.2.2.2, TS 29.513): on create, the PCF looks for the SM policy whose
  `ipv4Address` equals `ascReqData.ueIpv4`, and, when the request gives them, the same `dnn` and
  `sliceInfo` (NOTE 7: "all attributes must match, if provided"). No match, or more than one: 500
  with cause `PDU_SESSION_NOT_AVAILABLE` (TS 29.514 table 5.7.3-1). The binding is kept in its own
  table (`pcf::AppSessionBindings`), not in the stored AppSessionContext, so the GET representation
  stays TS 29.514's.
- **Termination** (TS 29.514 4.2.5.3, cause "PDU_SESSION_TERMINATION ... when the PCF received from
  the SMF the indication of SM Policy Context termination"): deleting an SM policy sends, for every
  AF session bound to it, a TerminationInfo (`termCause` PDU_SESSION_TERMINATION, `resUri` the app
  session's URI) to `{notifUri}/terminate`. The app session stays until the AF deletes it
  (4.2.4.2). With ADR-0520 the NEF turns this into the AF's SESSION_TERMINATION.

**Rejected alternatives.**
- *Binding on the UE address alone.* TS 29.514 NOTE 5/7: the same IPv4 address can exist in
  different slices/DNNs; given attributes must match.
- *Deleting the app session on termination.* The PCF requests termination; the consumer deletes.

**Disclosed.**
- Binding on `ueIpv6` / `ueMac` is not implemented (the SMF allocates IPv4 only): such requests fail
  binding with 500.
- Resource-allocation, QoS-notification, usage and access-type events still do not reach the AF:
  they need PCC rules installed at the SMF and the SMF's rule reports (next increment).
- The termination request is sent synchronously inside the SMF's DeleteSMPolicy; a slow AF delays
  that response. Not retried.
- The TSCTSF's own PCF callback (`/pcf-notify/{id}`) serves no `/terminate` (out of scope, as in
  ADR-0498).
- An app session whose PDU session ends stays bound to nothing; a later update is not re-bound.

**Verified (2026-10-24).** See below.
