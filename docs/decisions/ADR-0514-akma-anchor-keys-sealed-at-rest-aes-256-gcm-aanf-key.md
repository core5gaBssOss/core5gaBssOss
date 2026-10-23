## ADR-0514: AKMA anchor keys sealed at rest (AES-256-GCM, AAnF key-encryption key); per-NF Valkey ACL users, Valkey not published on the host

**Date:** 2026-10-23. **Status:** Proposed.

**Context.** REVIEW-5ae0ef1 SEC-04 (High): the AAnF stored every UE's KAKMA as hex in the lab's
single-node Valkey (`aanf:akid:<A-KID>` -> `{supi, kakma}`). That Valkey had no AUTH or ACL and
was published on host port 6379, and nearly every NF shares it. Anyone who could reach the host
port, and every NF container, could read any KAKMA and derive any AF's KAF, or plant contexts.
TS 33.117 4.2.3.2.3 requires restricted read access to sensitive data in storage, and that
authentication data is not stored in the clear. The review's "done when" asks two things:
`valkey-cli -h <host> -p 6379 keys 'aanf:*'` from outside the compose network fails, and stored
values are not raw keys.

**Decision:** the AAnF seals KAKMA with AES-256-GCM under a key-encryption key file, and every NF
on the lab Valkey authenticates as its own ACL user; Valkey is no longer published on the host
(approach user-chosen 2026-10-23: "AES-256-GCM, KEK file", over a dedicated AAnF Valkey and over
both).
- **KAKMA sealed at rest.** `libs/aka-crypto` gains `aes256gcm_seal` / `aes256gcm_open`
  (`aka_crypto/aead.hpp`; OpenSSL EVP, a 12-octet random IV per call; the stored form is
  IV || ciphertext || 16-octet tag). `aanf::AkmaStore` stores `{supi, kakmaSealed}`. The AAD is
  the record's key name plus its SUPI, so a sealed KAKMA copied into another record does not open.
  A record that does not open reads as absent: another KEK, a tampered value, or a
  pre-ADR-0514 plaintext `kakma`. If sealing fails, the register request is answered 500
  `SYSTEM_FAILURE` and nothing is written.
- **The KEK** is one line of 64 hex digits in a file: `kakma_kek_file` / `AANF_KAKMA_KEK_FILE`,
  default `<CERTS_DIR>/aanf/kakma.kek`. The AAnF exits at startup if the file is missing or
  malformed. `scripts/gen-lab-pki.sh` writes the file (mode 0640) when `aanf` is in its list, so
  CI's and the lab's PKI runs produce it.
- **Per-NF Valkey ACL users.** `nf_config::redis_auth(config, "<NF>")` reads `redis_user` and
  `redis_password_file` (environment `<NF>_REDIS_USER` / `<NF>_REDIS_PASSWORD_FILE` win). The
  password is read from a file, so it is in neither a config file nor the environment.
  `connect_redis_or_die` and `connect_redis_cluster_or_die` take it; every caller passes it (AMF,
  AUSF, CHF, NWDAF, DCCF, ADRF, MFAF, UDSF, AAnF, and LI-MDF / LI-MDF3, which now connect
  through `connect_redis_or_die` too). Neither key set means the URL's own credentials, else the
  default user, which is unchanged for host runs and CI.
- **The lab's ACL** (`scripts/gen-lab-pki.sh` -> `certs/valkey/users.acl`, SHA-256 hashes only;
  `certs/<nf>/valkey.password`, random, 0640):
  - `default` is off.
  - The AAnF's user may touch only `aanf:*`, and has neither `@admin` nor `@dangerous`.
  - Every other NF's user has `+@all -@admin -@dangerous -select -move -copy`, which pins it to
    logical database 0. `SWAPDB`, `MIGRATE` and `KEYS` are `@dangerous`.
  - The AAnF's contexts are in database 1 (`tcp://valkey:6379/1` in the lab config), so no other
    NF can reach them at all.
- **Compose.** The `valkey` service publishes no host port, loads `--aclfile`, and mounts only
  the `valkey` subpath of the certs volume, not every NF's private key. Its healthcheck accepts
  `PONG` or `NOAUTH`. `tools/lab/gen_compose_config.py` sets `redis_user` and
  `redis_password_file` in the lab config of every NF on the single-node Valkey. It skips a
  service whose environment points it elsewhere (the UDSF, on the Valkey Cluster).

**Rejected alternatives.**
- *A dedicated, password-protected AAnF Valkey, values left as hex* (user-rejected). This
  restricts access, but KAKMA would still be stored in the clear, which 4.2.3.2.3 also forbids.
- *A KEK in the config file or an environment variable.* Config files are committed, and the
  environment is visible to `docker inspect`. A file can be a Kubernetes Secret.
- *Per-NF key-prefix ACLs for every NF.* Prefixes found by grepping string literals proved
  unreliable: `"li-mdf:"` is a log tag, while the LI-MDF's keys are `limdf:`. A wrong pattern
  fails only at runtime with `NOPERM`. Database separation protects the one secret-bearing store
  without needing a prefix list for every NF.
- *Valkey `requirepass` (one shared password).* Every NF would hold the same credential.

**Disclosed.**
- *Lab only, not the Helm charts.* The PKI script and compose do this. The Helm charts on PR #42
  (ADR-0513) need `kakma.kek` and each NF's `valkey.password` added to the per-NF certs Secrets,
  plus a Valkey ACL. There is no Valkey chart yet (DEP-03). Until then a Helm AAnF exits at
  startup with "the KAKMA key-encryption key ... is missing".
- *The KEK is a file, not an HSM or KMS.* It is not rotated, and old records do not open under a
  new KEK; they read as absent and the UE re-runs primary authentication. The key is not
  zeroized in memory.
- *The lab's `certs_data` volume is still mounted into every NF container* (ADR-0019), so in
  compose any NF container can read the AAnF's KEK and every NF's Valkey password. The
  separation holds against the network and the host, not between lab containers. Helm's per-NF
  Secrets (ADR-0513) are the deployment that separates them.
- *The other NFs' users are not confined to their own key prefixes*, only kept out of database 1.
- *The Valkey Cluster (UDSF) is unchanged:* no ACL, ports 7001-7006 still published. It holds no
  key material.
- *Existing local stores:* a developer's `certs/` from before this ADR has no `kakma.kek`. Run
  `scripts/gen-lab-pki.sh aanf` once; it reuses the existing certificates.
- *Plaintext records written before this change* are not migrated; they read as unknown A-KIDs.
- `scripts/run-chf-rating-baseline.sh` expects a Valkey on host port 6379. The compose one is no
  longer published, so that script needs its own Valkey (`docker run -p 6379:6379
  valkey/valkey:8-alpine`).
- The RedisInsight console (host port 5540) now needs an NF's ACL user and password, and sees
  only what that NF may.

**Verified (2026-10-23, this host).**
- Build: `aka_crypto aanf amf ausf chf nwdaf dccf mfaf adrf udsf li-mdf li-mdf3 conformance_tests
  akma_e2e_integration_tests`, 47/47 steps, no failure.
- `Aes256Gcm.*` (4 tests: GCM test cases 13, 14, 16 known answers -- re-derived with Python
  `cryptography` 46.0.5 -- random-IV round trip, wrong key / AAD / every flipped octet refused):
  pass.
- ACL file from `scripts/gen-lab-pki.sh amf aanf` on a throwaway `valkey/valkey:8-alpine` (8.1.10)
  with `--aclfile`: unauthenticated GET -> `NOAUTH`; healthcheck command passes; AAnF user SELECT 1
  + SET `aanf:akid:x` OK, SET `amf:x` and FLUSHDB -> `NOPERM`; AMF user SET/GETDEL in db 0 OK,
  SELECT 1, COPY ... DB 1, MOVE, SWAPDB, KEYS, FLUSHALL, ACL LIST, CONFIG GET -> `NOPERM`; wrong
  password -> `WRONGPASS`.
- The built AAnF against that Valkey (`tcp://127.0.0.1:26379/1`, user `aanf`): "connected to
  Redis/Valkey" and listening; another NF's password or none -> exits `WRONGPASS`; a missing KEK
  file -> exits naming the file.
- `tools/lab/gen_compose_config.py`: 11 lab configs gain `redis_user` / `redis_password_file`
  (the AAnF's URL `/1`); `udsf.json` unchanged.
- Not run locally: `akma_e2e_integration_tests` (now also asserting the stored record holds
  `kakmaSealed` and no KAKMA hex) -- the CI runner was running its own ctest on this host; it
  runs in the PR's CI. Not run: `make lab-up` with the new Valkey.
