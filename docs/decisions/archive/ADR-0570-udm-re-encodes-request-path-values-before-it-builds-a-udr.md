## ADR-0570: UDM re-encodes request-path values before it builds a UDR URI

**Date:** 2026-11-08. **Status:** Closed (work pushed; last citing commit 69d51a54 on origin/main, 2026-11-10). Accepted (user: "go ahead with the UDM fix", 2026-11-08). Follow-up
to ADR-0549 (REVIEW-5ae0ef1 SEC-15).

**Context.** ADR-0549 made the sbi_core server percent-decode each path parameter after splitting
the path, so `imsi-1%2Fx` reaches a handler as the one value `imsi-1/x`. The UDM proxies most
of its Nudm operations to the UDR. It pasted the decoded `ueId` (and `servingPlmnId`,
`afInstanceId`, `extGroupId`, `sharedDataId`) into the UDR URI unchanged, so an encoded `/` or
`?` became a real one on the next hop. Example: `GET /nudm-sdm/v2/imsi-X%2F99970%2Fprovisioned-data%2Fsm-data%3F/am-data`
made the UDM request `/subscription-data/imsi-X/99970/provisioned-data/sm-data?/...`, and it returned
the subscriber's sm-data as if it were am-data. The two group-identifier query values went into the
UDR query string the same way. Found by `/code-review` on 2026-11-08.

**Decision:**

- Every value the UDM takes from its own request path or query and puts into a UDR URI is
  percent-encoded as one segment with `sbi_core::uri::encode_path_segment` (the helper `seg()` in
  `nfs/udm/src/main.cpp`, and in `UdrAuthSubscriptionSource::resource_url`). The value is encoded
  where the URI is built, not where it is read, because the same value is also a local key and a
  log field.
- `sbi_core::uri::encode_query_value` is new. It also encodes `&`, `=`, `+`, `;` and `#`, and it
  carries `ext-group-id` / `int-group-id` into the UDR query.

**Rejected alternatives.**

- **Rejecting any ueId that contains `/` or `?` at the UDM.** TS 29.571 lets a SUPI or GPSI carry
  any character (the reason ADR-0549 encodes), so a valid `nai-` SUPI would be refused.
- **Stopping the server from decoding.** That would undo ADR-0549, and every handler would then
  compare encoded ids.

**Disclosed.**

- Only the UDM is changed. Other NFs that build a peer URI from a decoded path parameter were not
  audited in this change. That audit is deferred for future implementation; the PCF and AAnF
  already encode (ADR-0549).
- Callback URIs that consumers supply are used as given, as before.

**Verified (2026-11-08, this machine).**

- `nfs/udm/src/main.cpp`, `udr_auth_source.cpp` and both changed test files compile
  (`-fsyntax-only` with the build's flags).
- Not run here, because CI is using this machine:
  `UdmUeauFromUdrIntegration.EncodedSlashInUeIdNeverReachesAnotherUdrResource` (the control asks
  for am-data with the plain SUPI and expects 200; the injected ueId must not return 200 or
  sm-data) and the new `encode_query_value` assertion in
  `Http2RoutePrecedence.AnEncodedPathParameterArrivesDecodedAsOneSegment`. CI runs both on the PR.
  The test has not yet been shown to fail without the fix.
- CI: PR #15 run 42499914588 passed 1232 of 1232 tests, and run 42547589719 on main (2026-11-09)
  passed 1245 of 1245 with every job succeeding (build, ASan/UBSan, TSan). Both runs include
  `EncodedSlashInUeIdNeverReachesAnotherUdrResource` and the `encode_query_value` assertion. The
  test has still not been shown to fail without the fix.
