# specs/tmforum/

TM Forum Open API definitions this repository generates code from or implements by hand (ADR-0558,
ADR-0568, ADR-0611). Each file is copied byte-for-byte from the TM Forum repository named below,
at the commit named, from the file of the same name at that repository's root. They are licensed
under the Apache License 2.0 (`LICENSE`, copied from the TMF640 repository; the `LICENSE` file of
each other repository below is byte-identical to it, SHA-256
`65ac57b1de61ae34baed5b654568d6799f8e5300ab75cf363d4a977b146fa4df`, checked 2026-11-21).

| File | Repository | Commit (date) | SHA-256 |
|---|---|---|---|
| `TMF640-ServiceActivation-v4.0.0.swagger.json` | https://github.com/tmforum-apis/TMF640_ActivationConfiguration | `44d0e9e81f3d27239fa31e4c86b568dd22eeacf1` (2026-08-07) | `9ed4d93bad94fde358aa646e04f0c412e3fcdc8f56e22dc2bf5b7f9a4e39c930` |
| `TMF620-ProductCatalog-v4.1.0.swagger.json` | https://github.com/tmforum-apis/TMF620_ProductCatalog | `24e69f3026f2b514baa27d7206540fc7b55bcb45` (2026-08-07) | `10db69222fcdbeb7e99b423af3e83953bcacc832278af944016cdc05a5ed60cc` |
| `TMF632-Party-v4.0.0.swagger.json` | https://github.com/tmforum-apis/TMF632_PartyManagement | `11c6a0ecf2b9a7007f43e86601b37e45d9f39b1e` (2025-11-07) | `866d72c02e3dc1e4baffb8db733b99fd2554ddc822bee6a020287037c33240af` |
| `TMF651-Agreement-v4.0.0.swagger.json` | https://github.com/tmforum-apis/TMF651_AgreementManagement | `008dae3a3f929c97a34f767d051981aadb285b6c` (2026-08-07) | `b55f1514d566c7dee1b08b2a81a3b6e2292f9da079041bc3a3ca12eb2a0fb88a` |
| `TMF654-PrepayBalance-v4.0.0.swagger.json` | https://github.com/tmforum-apis/TMF654_PrepayBalanceManagement | `562e655d14d796b8380b4e244a5b601f69d60b64` (2026-08-07) | `f397aea173cffeedb0bad4d7dd89ea3e469b881ed6e350828325c3ffa2751920` |

The TMF640 repository also holds `TMF640_Service_Activation_Management_API_v4.0.0_swagger.json`. Its
paths and definitions are identical to the file above (compared 2026-11-07, docs/PROVISIONING_MAPPING.md),
so it is not vendored.

The TMF620, TMF632, TMF651 and TMF654 files were fetched on 2026-11-21 from
`raw.githubusercontent.com` at the commit in their row, which was the head of `main` that day; the
date in brackets is that commit's date. The file itself last changed in an earlier commit
(TMF620 `4a76d6bc`, 2021-05-18; TMF632 `e4642b97`, 2020-10-09; TMF651 `17167ad9`, 2019-12-10;
TMF654 `5be484f9`, 2020-10-09). The git blob SHA-1 of each copy equals the one the GitHub contents
API reports for that path at that commit. These four are used by hand-written handlers in
`bss/` and `libs/bss-sid` today, not by `tmf_generated` (ADR-0611).

Each of those repositories also holds a `TMFnnn_<Name>_API_v4.x.x_swagger.json` copy
(`TMF620_Product_Catalog_Management_API_v4.1.0_swagger.json`,
`TMF632_Party_Management_API_v4.0.0_swagger.json`,
`TMF651_Agreement_Management_API_v4.0.0_swagger.json`,
`TMF654_Prepay_Balance_Management_API_v4.0.0_swagger.json`). At the same commits their `paths` and
`definitions` are identical to the file vendored here (compared 2026-11-21, ADR-0611), so they are
not vendored.
