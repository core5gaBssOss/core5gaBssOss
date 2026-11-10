## ADR-0568: sbi-codegen also generates the TM Forum DTOs from Swagger 2.0

**Date:** 2026-11-07. **Status:** Closed (work pushed; last citing commit 1e11ba6d on origin/main, 2026-11-10). Accepted (user approved increment 1's plan, 2026-11-07). Implements ADR-0558
decision 3, step P0.

**Context.** ADR-0558 requires the TMF640/TMF664 DTOs to be generated from the TM Forum swagger,
never hand-written. The TM Forum v4.0.0 files are Swagger 2.0: schemas live under `definitions`,
not `components.schemas`. They also use member names that are not C++ identifiers (`@type`,
`@baseType`, `@schemaLocation`, `@referredType`) and lowercase enum values, one of which is a C++
keyword (`OrderItemActionType` `delete`). The repo's other TM Forum APIs (TMF620 and the others)
handle their JSON by hand today. `tools/sbi-codegen` (ADR-0010) read only OpenAPI 3.

**Decision:**

- `tools/sbi-codegen` reads Swagger 2.0 files. A file whose top-level `swagger` member is `2.x`
  has its schemas read from `definitions`, and `#/definitions/X` refs resolve like
  `#/components/schemas/X`.
- `@name` members become `atName` in C++; the wire name is unchanged. Any other character that is
  not legal in an identifier becomes `_`. An enum value that is a keyword gets a trailing `_`, as
  field names already did.
- `generate.py` gains `--namespace`, `--provenance` and `--specs-path`. The defaults are the old
  hard-coded 3GPP values. TM Forum output goes to namespace `tmf_gen` with its own provenance line.
- A `.swagger` file's output stem drops `.swagger`, and its `-`/`.` become `_`
  (`TMF640_ServiceActivation_v4_0_0.hpp`). 3GPP stems are unchanged, `-` included.
- `specs/tmforum/` vendors `TMF640-ServiceActivation-v4.0.0.swagger.json` byte for byte. Its
  provenance (commit `44d0e9e`) and SHA-256 are in `SOURCES.md`, and the Apache-2.0 `LICENSE` is
  copied from tmforum-apis.
- `libs/tmf-generated` builds `tmf_generated` with the same staging and sync pattern as
  `sbi_generated` (ADR-0360).

**Rejected alternatives.**

- **openapi-generator for the TM Forum files.** It was rejected for 3GPP in ADR-0010. Using it here
  would bring a second generator, a Java toolchain and a second serde style.
- **Converting the swagger to OpenAPI 3 first (swagger2openapi).** The vendored file would then not
  be the published one, and the conversion would be an unreviewed transform on every regeneration.
- **Hand-written TMF640 structs, like TMF620.** ADR-0558 decision 3 forbids it.

**Disclosed.**

- TM Forum enums generate as the generator's *open* enums, so an unknown string is accepted on
  parse, exactly as for 3GPP. The swagger's closed enums (`ServiceStateType` etc.) are enforced by
  the TMF640 handler (P1), not by the DTO.
- `Any` (`Characteristic.value`) is `{}` in the swagger and generates as `nlohmann::json`.
- Only TMF640 is vendored. TMF664 follows in increment 2.
- The existing TM Forum APIs stay hand-handled JSON. Moving them onto generated DTOs is deferred
  for future implementation.

**Verified (2026-11-07, this machine).**

- CI run 42391980707 (PR #96, 2026-11-07): configure, full build, ctest, TSan and ASan/UBSan jobs
  all succeeded. The job log could not be downloaded afterwards, so the per-test lines for
  `test_tmf640_dtos` are not quoted here.
- All 165 3GPP pilot files in `libs/sbi-generated/CMakeLists.txt` were regenerated with the old and
  the new generator. `diff -rq` reports the 249 output files byte-identical. The first attempt was
  not identical: the stem change renamed `TS29511_N5g-eir_EquipmentIdentityCheck`. That is why
  only `.swagger` stems are rewritten.
- The TMF640 output (48 object, 2 enum and 1 opaque type) compiles with the build's own flags and
  `-Werror`.
- Without this change the generator reads 0 types from the TMF640 file (`sbi-codegen: 0 types
  -> 0 files`, run with the `origin/main` copy of the tool).
- `test_tmf640_dtos` (2 tests) was compiled with those flags, linked against the vcpkg GoogleTest,
  and passed. A full `Service` with `@type`, `@baseType`, `@referredType` and a structured
  characteristic value round-trips to equal JSON.
- CMake configure and the full CI build have not run yet on this change; that result comes from
  CI on the PR.
