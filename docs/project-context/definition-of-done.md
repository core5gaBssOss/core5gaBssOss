# Definition of done (per NF)

Moved verbatim from CLAUDE.md (section "Definition of done (per NF)", full text); see `docs/optimization/MOVE_LOG.md`.

1. API generated from the R19 YAML, with source file + branch cited.
2. NRF registration/discovery/heartbeat working, OAuth2 token validated.
3. All mandatory TS 23.502 procedures for that NF implemented + tested.
4. `ProblemDetails` error handling per TS 29.500.
5. OpenTelemetry spans + Prometheus metrics emitted.
6. Conformance test against the generated OpenAPI schema (request AND
   response).
7. [Opening of item 7 (the `config:ro` mount rule, ADR-0453) is kept once, verbatim, in `CLAUDE.md` section "Definition of done (per NF)" -- not repeated here.] A Dockerfile `COPY`ing the config
   file into the image is not an acceptable substitute -- the bind
   mount is the one, only mechanism, so a config edit takes effect on
   restart without an image rebuild, same as every other runtime value
   in this project.
8. Spec-traceability doc entry: procedure -> TS clause -> source file ->
   test.
9. Architecture diagram and product/license table in `README.md` updated:
   the NF's box goes from dashed to solid, new datastores/products get a
   row with their real license (conventions in `docs/ARCHITECTURE.md`).

## Walking the list (moved from the former `new-nf-checklist` skill)

Item 7 is the one that has bitten before: the compose entry's `volumes:` MUST include `- ../../config:/build/config:ro` from the moment it is written (ADR-0453); a Dockerfile `COPY` is not a substitute.
Before writing a new NF, show the TS 23.502 procedure list and get approval (CLAUDE.md "Working style").

## Never repeat these (docs/reviews/REVIEW-5ae0ef1.md) -- check every new or changed handler/call before committing
1. **PATCH**: apply the body as the YAML defines it -- `json-patch+json` = RFC 6902 `PatchItem[]`; a `*Patch`/`*UpdateData` wrapper (e.g. `{"ascReqData":...}`, `5gLanParamsPatch`) is unwrapped, never merged whole; wrong content type = 415. (SPEC-02/03/04)
2. **No invented names on the wire or in comments**: every JSON key, schema name, cause and enum is grepped in `specs/` first; no `_private` relay fields. (SPEC-01/04/06/10)
3. **Callbacks**: the URI is exactly the YAML callback expression, suffix included (`{notificationUri}/update`, `/terminate`); `Location` is `{apiRoot}/...` absolute, and consumers accept an absolute `Location`. (SPEC-05, SBI-05, SEC-07)
4. **Auth**: no token is not "valid"; 401 carries `WWW-Authenticate: Bearer`; every outbound SBI call sends a token for the target service; notifications are exempt (TS 33.501 13.4.1.0). (SBI-01/02, SEC-03)
5. **Errors**: ProblemDetails carries a TS 29.500 table 5.2.7.2-1 `cause`; status codes are only those the YAML lists (no 201 where the YAML has 200/204). (SBI-03, SPEC-07)
6. **Robustness**: fail closed on a peer failure unless a disclosed policy says otherwise; bounded queues/threads/stores; constant-time compare for secrets; no secrets in clear in shared stores. (SEC-04/06/10/12, SUS-02/04)
7. **Deployment**: config URLs work inside compose (service DNS, not 127.0.0.1) and Helm mounts config + certs. (DEP-01/02)
8. **Verification**: a test that fails without the fix (checked), YAML-shape conformance for new bodies, and an ADR "Verified" line naming only tests that exist and pass.
