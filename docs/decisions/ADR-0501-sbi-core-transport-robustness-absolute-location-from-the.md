## ADR-0501: sbi-core transport robustness: absolute Location from the request authority, client timeouts, request-body limit, never-destroyed token statics

**Date:** 2026-10-18. **Status:** Accepted (approach for SBI-05 chosen by the user, 2026-10-18);
implemented in PR #9, CI pending at the time of writing.

**Context.** `docs/reviews/REVIEW-5ae0ef1.md` items SEC-08 (no client timeout), SEC-09 (no request
body limit), SBI-04 (an unroutable request was always 404), SBI-05 (~100 bare-path `Location`
headers where every YAML describes `{apiRoot}/...`) and SEC-07 (the AAnF built a DELETE URL as
`udm_base + Location`). All of them live in, or are best fixed in, `libs/sbi-core`, which every NF
links. Separately, `main`'s ASan leg found a use-after-free in ADR-0499 phase 5's
`clear_default_token_backoff()` at process exit.

**Decision.**
1. **SBI-05: one rewrite in the sbi-core server, not per-site edits.** In `submit_response`, a
   `location` header whose value starts with `/` becomes `https://<the request's :authority><path>`
   -- the apiRoot structure of TS 29.501 4.4.1 (scheme, "://", authority, optional API prefix;
   `specs/3gpp/TS_29.501_j50.txt:451-456`). An absolute value is passed through. The apiRoot is
   the authority the consumer used to reach the NF, so it is reachable by construction. Every NF
   consumer except the AAnF already accepted an absolute `Location` (surveyed); the AAnF now does,
   and logs a failed DELETE.
2. **SEC-08: client timeouts by default** -- connect 5 s, whole request 30 s -- configurable per
   process (`SBI_CLIENT_CONNECT_TIMEOUT_MS`, `SBI_CLIENT_TIMEOUT_MS`) or per client
   (`Client::set_timeouts`). A timeout is an error from `send()`.
3. **SEC-09: a request-body limit by default** -- 16 MiB, `SBI_MAX_BODY_BYTES` or
   `Server::set_max_body_bytes`. Data beyond it is discarded as it arrives (memory stays bounded) and
   the request is answered 413 with cause `MAX_JSON_SIZE_EXCEEDED` (TS 29.500 table 5.2.7.2-1).
4. **SBI-04: 501, 405 + `Allow`, then 404** (TS 29.500 5.2.7.2): a method no route serves is 501; a
   path served for other methods is 405 listing them; only an unknown path is 404. No cause on 501
   or 405 (that clause's NOTEs 1-2).
5. **Token-source statics are never destroyed.** The two mutexes and two `shared_ptr` slots behind
   `install_default_token_source` / `Client::set_default_token_source` are held in
   `sbi_core::detail::NoDestroy` (private, static storage, no heap allocation): an NF's detached NRF
   lifecycle thread may finish NFRegister while `main`'s return runs static destructors.

**Rejected alternatives.**
- **SBI-05 per-site edits** (the review's literal fix hint): ~100 sites, and every new handler could
  regress it; one rule in the server cannot.
- **SBI-05 apiRoot from configuration** (each NF's advertised address from `config/<nf>.json`):
  stable behind proxies, but needs every NF to hand its apiRoot to the server and can disagree with
  the address the consumer actually used. Rejected by the user in favour of the request authority.
- **Making the NRF lifecycle threads joinable** instead of never-destroyed statics: the right
  long-term shape, but it touches every NF's `main` (each detaches its own thread) -- not done in
  this change; the statics fix removes the use-after-free for all of them at once.
- **No default client timeout** (leave it to each caller): the review's failure is exactly that no
  caller set one.

**Disclosed limits.**
- Behind an SCP or load balancer, the apiRoot is the authority that reached the NF (the proxy's),
  not necessarily the producer's own; TS 29.500's indirect-communication rules for `Location` are
  not implemented here.
- The lab host is an IP literal; TS 29.501 says the host should be the operator FQDN.
- No API prefix is supported (none is configured anywhere in this project).
- The timeout and body-limit defaults are engineering choices, not measured or spec-given values.
- Verification of the use-after-free fix is CI's ASan leg only; no local ASan build exists.
