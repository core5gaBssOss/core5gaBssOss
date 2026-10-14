## ADR-0492: SEPP increment 2 -- telescopic FQDN: Nsepp_Telescopic_FQDN_Mapping and routing (TS 29.573 5.4, TS 29.500 6.1.4.3.2)

**Date:** 2026-10-14. **Status:** Accepted -- built (the increment listed in ADR-0491; started on the user's instruction, 2026-10-14).

**Context.** ADR-0491 left the telescopic-FQDN alternative out: an NF of this PLMN could reach a
foreign NF only with 3gpp-Sbi-Target-apiRoot. TS 29.500 6.1.4.3 says the SEPP "shall support" both
the telescopic FQDN with a TLS wildcard certificate and the header. Specs: TS 29.573 j70 5.4,
TS29573_SeppTelescopicFqdnMapping.yaml (REL-19, info.version 1.3.1; added to codegen), TS 23.003
j80 28.5.2, TS 29.500 j70 6.1.4.3.2-6.1.4.3.4.

**Decisions.**
1. `GET {apiRoot}/nsepp-telescopic/v1/mapping` on the SBI port: `foreign-fqdn` (an FQDN of another
   PLMN) -> TelescopicMapping {telescopicLabel, seppDomain = this SEPP's FQDN} (5.4.2);
   `telescopic-label` -> {foreignFqdn} (5.4.3); both or neither -> 400 INVALID_QUERY_PARAM; an FQDN
   of this PLMN or none -> 400; an unknown label -> 404 RESOURCE_URI_STRUCTURE_NOT_FOUND.
2. The label: "tel-" + 16 hex digits of SHA-256 of the lower-cased foreign FQDN. TS 23.003 28.5.2
   NOTE 1 leaves the construction to the implementation; a hash is one DNS label, far below 63
   octets for any FQDN, and every SEPP replica derives the same label without shared state. Labels
   are resolvable back only once handed out (mapping API or a rewritten Location).
3. Routing: a request whose :authority is `<label>.<SEPP FQDN>` targets `https://<foreign FQDN>`
   (default port; a telescopic FQDN carries none). It wins over a 3gpp-Sbi-Target-apiRoot header,
   which is then ignored (6.1.4.3.3 NOTE 1). On N32-f the sending SEPP inserts the header with the
   derived apiRoot (6.1.4.3.4, both SEPPs support it). The receiving SEPP's `local_nf_routes`
   lookup tries "host" and "host:443" for a port-less target.
4. A response's Location header naming the other PLMN is rewritten into telescopic form when the
   request came telescopically (6.1.4.3.2's link-relation case).
5. `libs/sbi-core`: `Request::authority` (the :authority pseudo-header) and
   `ClientRequest::resolve` (curl's CURLOPT_RESOLVE; the lab's stand-in for DNS of 3gppnetwork.org
   names -- TLS still verifies the name). Lab PKI: the `sepp` certificate adds
   `*.<SEPP FQDN>` for both lab SEPPs (TS 33.501 13.1's wildcard).

**Disclosed.** URIs inside message bodies (NF profiles in a discovery result, callback URIs in
subscriptions) are not rewritten -- only the Location header; so an NF following a URI it found
in a body reaches the foreign FQDN, not the telescopic one. An unknown label is not looked up at
other SEPPs of the PLMN (5.4.3 as client): there is one SEPP. The table of handed-out labels is in
memory (a restarted SEPP answers 404 for labels it gave out before; the label itself is
recomputable by asking again). The "transient phase" rule (skip conversion for requests that came
with the header) holds trivially: nothing is converted on that path.

**Rejected alternatives.** A reversible encoding of the FQDN in the label (e.g. base32): an
inter-PLMN FQDN of more than ~39 characters overflows the 63-octet label. Replacing dots with
dashes: ambiguous, since FQDN labels contain dashes. A random label: replicas would disagree.

**Verified.** `SeppN32.TelescopicLabelsAreSingleStableLabels`; `structural_conformance` (both
TelescopicMapping forms against TS29573_SeppTelescopicFqdnMapping.yaml);
`SeppEndToEnd.TelescopicFqdnReachesTheNrfOfPlmnB` (mapping, then GET to `<label>.<SEPP A FQDN>`
verified against the wildcard certificate, PLMN B's real NRF answers through both SEPPs; reverse
mapping; unknown label -> 404); the ADR-0491 tests unchanged.
