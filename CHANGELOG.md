# Changelog

Notable changes to this project. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and versions follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html): while the version
is 0.x, a minor release may break configuration or APIs. How releases are cut:
[`docs/RELEASING.md`](docs/RELEASING.md). Each architectural change has an ADR in
[`docs/DECISIONS.md`](docs/DECISIONS.md); entries here link the ADR rather than repeat it.

## [Unreleased]

### Changed

- The README is the full project page again, updated for the organization move, licensing and
  process changes, with four stale security-compliance rows corrected
  ([ADR-0511](docs/decisions/ADR-0511-readme-restored-to-the-full-project-page-updated-reverses.md)).

### Security

- The AAnF stores KAKMA sealed with AES-256-GCM under a key-encryption key file, never in the
  clear. In the lab, each NF signs in to Valkey as its own ACL user, and Valkey is no longer
  published on the host
  ([ADR-0514](docs/decisions/ADR-0514-akma-anchor-keys-sealed-at-rest-aes-256-gcm-aanf-key.md)).
  An existing local `certs/` needs `scripts/gen-lab-pki.sh aanf` once, to create the key.

## [0.1.0] - not yet tagged

The first tagged release. It records the state of the project after its first eleven weeks of
development (from 2026-08-04): a reference implementation that aims at production grade and is
**not deployable in production yet** (see [`docs/COMPLIANCE_P1_P15.md`](docs/COMPLIANCE_P1_P15.md)).

### Added

- SBI framework (`libs/sbi-core`): HTTP/2 server and client, TLS 1.3 with mTLS, OAuth2 tokens from
  the NRF, `ProblemDetails`, `3gpp-Sbi-*` headers, logging and tracing.
- Code generation of every NF's API from the 3GPP Release 19 OpenAPI YAML (`tools/sbi-codegen`).
- Control plane: NRF, AMF, SMF, UDM, UDR, AUSF and PCF. UE registration and PDU session
  establishment run end to end over real NGAP and NAS.
- User plane: PFCP (N4) and a UPF with an eBPF/XDP uplink GTP-U path. This is a lab fixture, not a
  gNB-facing N3.
- Charging: the CHF of TS 32.290/32.291, with Diameter Gy/Rf, Sy and CAMEL/CAP, verified end to end
  in the lab.
- BSS: TM Forum SID-aligned product catalog (TMF620), subscriber management (TMF632), roaming
  interconnect (TMF651) and balance management (TMF654).
- NWDAF (AnLF and MTLF), DCCF, MFAF and ADRF; AI-assisted quota sizing in the CHF, off by default.
- Release 19 NFs: 10 of 16 Tier 2 NFs, and the SEPP, AAnF and TSCTSF in part. Lawful Interception:
  ADMF, MDF2/MDF3 and IRI-POIs in several NFs.
- Operator console: React and JSON Forms, behind a C++ backend-for-frontend with Keycloak login.
- Lab packaging: Docker Compose and Helm charts, in part.
- Project setup:
  - `NOTICE`, `REUSE.toml` and `LICENSES/`, compliant with REUSE 3.3 (ADR-0507).
  - Display name "AI-Boosted 5G-Advanced Core, BSS and OSS" and a shorter README (ADR-0508).
  - `CODEOWNERS`, area and size labels, Dependabot, OpenSSF Scorecard, Markdown lint and this
    changelog (ADR-0509).

### Removed

- 3GPP and ETSI specification documents, from the tree and from the whole git history
  (ADR-0507). `tools/specs/fetch_3gpp_specs.py --from-manifest` fetches the versions the code
  cites.

### Security

- GitHub private vulnerability reporting, code scanning (CodeQL), Dependabot alerts and secret
  scanning with push protection are enabled ([`.github/SECURITY.md`](.github/SECURITY.md)).

[Unreleased]: https://github.com/core5gaBssOss/core5gaBssOss/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/core5gaBssOss/core5gaBssOss/releases/tag/v0.1.0
