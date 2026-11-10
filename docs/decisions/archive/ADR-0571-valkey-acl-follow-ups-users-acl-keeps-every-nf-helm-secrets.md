## ADR-0571: Valkey ACL follow-ups: users.acl keeps every NF, Helm Secrets carry the ACL file, compose points at the lab overlay

**Date:** 2026-11-09. **Status:** Closed (work pushed; last citing commit c1334bdf on origin/main, 2026-11-10). Accepted (user: "Please move to next item", 2026-11-09, the
review follow-ups planned on 2026-11-08). Follow-up to ADR-0514 (REVIEW-5ae0ef1 SEC-04).

**Context.** ADR-0514 gave every NF on the lab Valkey its own ACL user and turned the default user
off. `/code-review` (2026-11-08) found three deployment paths that this left broken:

- `scripts/gen-lab-pki.sh` rewrote `certs/valkey/users.acl` from only the NFs named in that run, so
  `gen-lab-pki.sh aanf` dropped every other NF's user, and they failed AUTH at the next Valkey
  restart.
- The Valkey NF charts (`deploy/helm/{amf,ausf,chf,nwdaf,nwdaf-mtlf,dccf,adrf,li-mdf,li-mdf3,mfaf,aanf}`)
  mount `valkey.password` (and the AAnF chart `kakma.kek`) as required Secret items.
  `scripts/helm/lab-pki-secrets.sh` added them only if the file existed, and wrote nothing for the
  Valkey side: a cluster Valkey had no way to get the ACL users, so every Valkey NF would fail
  WRONGPASS.
- `deploy/docker/docker-compose.yml` (and README "Lab:") said to run that file alone, but only the
  generated overlay `docker-compose.lab.yml` (`make lab-up`, ADR-0503) points the NFs at configs
  with `redis_user`/`redis_password_file`, so alone the Valkey NFs fail NOAUTH.

**Decision:**

- `gen-lab-pki.sh` writes `users.acl` for every Valkey NF named in the run and every one that
  already has `certs/<nf>/valkey.password`.
- `lab-pki-secrets.sh` exits with an error when a Valkey NF's `valkey.password` or the AAnF's
  `kakma.kek` is missing, and it also writes a Secret `valkey-users-acl` (key `users.acl`) when any
  Valkey NF is listed, for the operator's Valkey to load with `--aclfile`. The chart items stay
  required: a pod that cannot authenticate should not start.
- The compose header and README "Lab:" line say to use `make lab-up` for the whole lab; the README
  line is extended, not replaced (guardrail 10).

**Rejected alternatives.**

- **Making the Secret items `optional: true`.** The pod would start without its password and fail
  at its first PING instead, which is a later, less clear failure.
- **Putting `redis_user`/`redis_password_file` in `config/*.json`.** The same files are used for NFs
  run as host processes and by tests against a Valkey or Redis without these users, so they would
  all fail AUTH.
- **Adding a Valkey chart now.** In-cluster datastores are a separate, larger change (DEP-03).

**Disclosed.**

- No chart in this repo deploys Valkey; `valkey-users-acl` is for the Valkey the operator runs.
  That chart is deferred for future implementation (DEP-03).
- `scripts/pipeline-clean.sh` runs `redis-cli` inside the Valkey container with no credentials, so
  since ADR-0514 its `chf:*` key count and delete get NOAUTH. Not changed here; deferred for future
  implementation.
- The Valkey NF list is written twice (`VALKEY_NFS` in `gen-lab-pki.sh`, `is_valkey_nf` in
  `lab-pki-secrets.sh`); a comment ties them together.

**Verified (2026-11-09, this machine, in a scratch copy of the scripts, not the repo's `certs/`).**

- `gen-lab-pki.sh nrf amf chf aanf`, then `gen-lab-pki.sh aanf`: `users.acl` still has `amf`,
  `chf`, `aanf` (the `origin/main` script, same runs: only `aanf`).
- `lab-pki-secrets.sh` (all charts): 35 YAML documents parse; 10 `valkey.password` keys, 1
  `kakma.kek`, 1 `valkey-users-acl`. `lab-pki-secrets.sh nrf smf`: no Valkey keys or ACL Secret.
- Not run in a Kubernetes cluster.
