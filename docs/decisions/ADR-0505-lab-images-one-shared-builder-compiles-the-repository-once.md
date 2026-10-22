## ADR-0505: Lab images: one shared builder compiles the repository once for all NF images

**Date:** 2026-10-22. **Status:** Accepted.

**Context.** ADR-0503 (Disclosed): each `deploy/docker/<nf>.Dockerfile` compiles the whole
repository in its own `builder` stage and copies one binary into its `runtime` stage, so
`make lab-build` of the 35 built compose services compiles the repository ~35 times ("many
hours"); the lab therefore ran 7-day-old images without ADR-0499..0502, and the SMF, AAnF, SEPP
and TSCTSF images were stale or missing. ADR-0503 named "a shared builder stage" as the next step.
Checked here: the 34 builder stages are identical apart from `cmake --build build --target <nf>`
(comments aside), and every runtime stage takes its build output only through
`COPY --from=builder /build/...`.

**Decision:** `tools/lab/gen_shared_builder.py` generates, from the Dockerfiles on every run,
`build/lab-images/builder.Dockerfile` (the common builder stage with `--target` replaced by all 34
targets) and a compose override giving each service with a `builder` stage the build context
`builder: docker-image://5gc-lab-builder:local`. BuildKit uses a named build context in place of
the Dockerfile stage of the same name, so each image's runtime stage copies from the one shared
builder image and its own builder stage never runs. `make lab-build-fast` runs the generator,
builds the builder image once, then `docker compose ... build` with the override. The generator
refuses to run if the builder stages ever differ beyond `--target`, so the shared builder cannot
silently build something a Dockerfile would not.

- The NF Dockerfiles are unchanged; `make lab-build` (each image compiles on its own) still works
  and remains the reference.
- The generated files live under `build/` (git-ignored), so there is nothing to drift and no CI
  check to add.
- The builder keeps the Dockerfiles' Release configuration, ccache mount (`id=ccache`) and vcpkg
  binary cache, so a rebuild recompiles only what changed.

**Rejected alternatives.**
- *Package host-built binaries (`build/`) into the runtime images.* The host is Ubuntu 26.04 like
  the images, but the host build is Debug and its binaries carry compiled-in absolute host paths
  (RUNPATH `/home/.../build/libs/li-core`; default certificate, CA and config paths under the
  source tree), where the containers use `/build/...`. Making that work means rewriting paths
  inside binaries.
- *Edit the 34 Dockerfiles to `FROM` a shared builder image.* A standalone `docker build` of one
  NF would then need the shared image first; the named-context override keeps each Dockerfile
  self-contained.
- *A committed `builder.Dockerfile`.* A hand-maintained copy of the builder stage drifts; generated
  on every run from the Dockerfiles it cannot.

**Also in this ADR (found bringing the lab up).**
- `make lab-registered`: every service whose lab config names an NRF (26), and whether its own log
  shows a successful NFRegister -- the ADR-0503 "lab-status reports container state, not
  registration" gap. From the NF's log, not an NRF query (that needs a token and a client
  certificate).
- `doris-schema-init` waited for "any `true` in the SHOW BACKENDS row" (ADR-0503). The Status
  column's JSON carries `"isActive":true` before the backend is alive, so the wait passed early and
  the first CREATE TABLE failed with "available backend num is 0". It now waits for the `Alive`
  column (`SHOW BACKENDS\G`, `Alive: true`).

**Verified (2026-10-22, this host, CI runner stopped for the run).**
- Shared builder: the 34 targets compiled in one `docker build` (2658 ninja steps, compile step
  1459 s, mostly ccache hits from earlier per-NF builds; image export 223 s). The 35 images then
  built in ~7 minutes with 0 steps of any per-image builder stage executed. Binaries in the SMF,
  SEPP, AAnF and TSCTSF images are byte-identical (sha256) to the shared builder's; `ldd` finds no
  missing library in the SMF, CHF, AMF, NWDAF, UPF, NRF and OAM GUI BFF images.
- `make lab-up` (second run, after the Doris wait fix): every service running, every one-shot
  init exited 0. `make lab-registered`: 26 of 26 registered (ADR-0503's run: 22; the SMF, AAnF,
  SEPP and TSCTSF now included). The NRF's own log shows 26 distinct NF instances registered.
- Found, not fixed here: the EIR is refused every access token ("the client certificate does not
  name the NF type"). ADR-0499's binding compares the certificate's CN/DNS name with the lower-cased
  NF type, and the lab certificate says `eir` while the TS 29.510 NFType is `5G_EIR`.

**Disclosed.**
- One builder image holds every NF's build tree (13.3 GB); it is a build-time image only, never run
  in the lab.
- The builder compiles with all host cores unless `LAB_BUILD_JOBS` is set; on the shared CI host it
  competes with CI jobs for CPU and memory.
- Still not verified through the lab: a UE registration or PDU session; the UPF's eBPF/XDP datapath
  in its unprivileged container (ADR-0503).
