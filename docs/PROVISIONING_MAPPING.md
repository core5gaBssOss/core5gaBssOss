# Provisioning Mapping: TMF640 Service / TMF664 ResourceFunction → 3GPP data

**Status: DRAFT for review (ADR-0558 decision 4).** On 2026-11-07 the user approved ADR-0558's
direction: TMF640 for subscriber-service activation, and TMF664 → TS 28.532 provisioning MnS on the
TS 28.541 NRM. This table did not exist then, so that approval covers the direction, not these rows.
Rows marked **TODO** are open questions for the user. No mapping code exists or will be written
until the rows are approved. Same rule as `docs/CHARGING_MAPPING.md`: every field below was read
from a fetched or vendored file, and nothing is recalled.

## Sources

| Side | File | Version / commit |
|---|---|---|
| TM Forum | `tmforum-apis/TMF640_ActivationConfiguration` `TMF640-ServiceActivation-v4.0.0.swagger.json` | 4.0.0 @ `44d0e9e` (fetched 2026-11-07) |
| TM Forum | `tmforum-apis/TMF664_ResourceFunctionActivationConfiguration` `TMF664-ResourceFunctionActivation-v4.0.0.swagger.json` | 4.0.0 @ `2f7bb57` (fetched 2026-11-07) |
| 3GPP | `specs/5G_APIs-REL-19/TS28532_ProvMnS.yaml` | REL-19 (vendored) |
| 3GPP | `specs/5G_APIs-REL-19/TS28541_5GcNrm.yaml`, `TS28541_SliceNrm.yaml` | REL-19 (vendored) |
| 3GPP | `specs/5G_APIs-REL-19/TS28623_GenericNrm.yaml`, `TS28623_ComDefs.yaml` | REL-19 (vendored) |
| 3GPP | Today's subscriber data path: `bss/provisioning/src/udr_adapter.hpp` (TS29503 / TS29505 / TS29519 documents via the UDR's project-owned `PUT /oam-provisioning/v1/subscribers/{ueId}`) | this repo |

### Which swagger file (ADR-0558 asked for the diff)

Each repo holds two v4.0.0 files. They were compared after flattening `allOf`:

- **TMF640**: `TMF640-ServiceActivation-v4.0.0.swagger.json` and
  `TMF640_Service_Activation_Management_API_v4.0.0_swagger.json` have identical `paths`,
  `definitions`, `basePath` and `info`. Either can be used; this table cites the first.
- **TMF664**: the files differ. The `..._Management_API_...` file adds `Addressable`, `Entity` and
  `Extensible`. 46 definitions still differ after flattening. For example, `href`/`id` are present
  or absent on the event and ref types, `ResourceFunction_Update.usageState` exists only in the
  first file, and the `TaskStateType` enum differs: `acknowledged` in the first file, `accepted`
  in the second.
  **TODO-F1: which TMF664 file is normative?** Proposed: `TMF664-ResourceFunctionActivation-v4.0.0`,
  the file ADR-0558 cites and the counterpart of the TMF640 file. This table uses it.

TMF640/TMF664 v5: the public `tmforum-apis` org has only these two repositories for 640/664 (GitHub
search, 2026-11-07), both v4.0.0. Whether TM Forum publishes a v5 elsewhere is still not checked.

## Part A: TMF640 `Service` → subscriber data in the UDR (increment 1)

TMF640 `Service` has no required properties in the swagger. `ServiceStateType` is
`feasibilityChecked, designed, reserved, inactive, active, terminated`.

| TMF640 `Service` field | 3GPP target | Rule | Status |
|---|---|---|---|
| `id`, `href` | none; TMF640 resource identity | Assigned by the TMF640 component; kept with the `ueId` it activated | Proposed |
| `serviceSpecification` (`ServiceSpecificationRef`) | Network profile → `AccessAndMobilitySubscriptionData.nssai`, `SessionManagementSubscriptionData` (`dnnConfigurations`, S-NSSAI), `subscribedUeAmbr` | Replaces today's key, the product offering id in `network_profiles`. The profile itself stays in config (ADR-0382) | **TODO-A1**: key the profile by ServiceSpecification id instead of offering id? |
| `serviceCharacteristic[name=supi]` | `ueId` (SUPI) of every document written | `Characteristic` is free-form name/value; the name is a project choice | **TODO-A2**: characteristic names. Proposed: the 3GPP attribute names (`supi`, `msisdn`) |
| `serviceCharacteristic[name=msisdn]` | GPSI `msisdn-<digits>` in `AccessAndMobilitySubscriptionData.gpsis` | As `udr_adapter` does today | Proposed (with A2) |
| SIM credentials K, OPc, SQN | `AuthenticationSubscription` (TS29505) | Today they come from the order, are used once, and are never persisted. A TMF640 `serviceCharacteristic` is readable back with `GET /service/{id}` | **TODO-A3**: credentials must not be stored in or readable from a `Service`. Proposed: they keep travelling on the order/saga path, outside TMF640 |
| `state` → `active` | `PUT /oam-provisioning/v1/subscribers/{ueId}` succeeds | The saga's existing UDR step; a failure leaves the state unchanged and fails the TMF641 item | Proposed |
| `state` → `feasibilityChecked` / `designed` / `reserved` | none | No network write | Proposed |
| `state` → `inactive` | No barring field chosen. 3GPP options (e.g. operator-determined barring) not yet looked up | | **TODO-A4**: what does "inactive" mean in the network? |
| `state` → `terminated` | none today: the UDR OAM API has only `PUT` | Deprovisioning is deferred for future implementation | **TODO-A5**: deprovisioning is out of scope for increment 1? |
| `startDate`, `endDate` | none | Kept by TMF640 only | **TODO-A6**: should they schedule activation? Proposed: no |
| `category` | none | TMF640 activates resource-facing services | **TODO-A7**: accept `category` only as `RFS`? |
| `serviceOrderItem`, `relatedParty`, `place`, `note`, `feature`, `serviceRelationship`, `supportingService`, `supportingResource`, `isBundle`, `isServiceEnabled`, `isStateful`, `hasStarted`, `startMode`, `serviceDate`, `serviceType`, `name`, `description`, `relatedEntity` | none | Kept by TMF640 only; no 3GPP field they drive | Proposed |

## Part B: TMF664 `ResourceFunction` → TS 28.541 NRM via TS 28.532 ProvMnS (increment 2)

TMF664 `ResourceFunction` requires `href` and `id`. TS 28.532 ProvMnS (`TS28532_ProvMnS.yaml`) has
these operations on `/{className}={id}`: `put` ("Replaces a complete single resource or creates it
if it does not exist"), `get`, `patch` and `delete`. Its notifications are `notifyMOICreation`,
`notifyMOIDeletion`, `notifyMOIAttributeValueChanges` and `notifyMOIChanges`. Nothing in this repo
implements ProvMnS today (ADR-0558 "Disclosed").

### Operations

| TMF664 | TS 28.532 ProvMnS | Status |
|---|---|---|
| `POST /resourceFunction` | `PUT /{className}={id}` (create) | Proposed |
| `GET /resourceFunction/{id}` | `GET /{className}={id}` | Proposed |
| `PATCH /resourceFunction/{id}` | `PATCH /{className}={id}` | Proposed |
| `DELETE /resourceFunction/{id}` | `DELETE /{className}={id}` | Proposed |
| `hub` + `/listener/resourceFunction{Create,AttributeValueChange,StateChange,Delete}Event` | `notifyMOICreation`, `notifyMOIAttributeValueChanges`, `notifyMOIDeletion` (subscription via TS 28.623 SubscriptionControl NRM) | **TODO-B1**: confirm the event pairing, and that StateChange maps to AttributeValueChanges on the state attributes |
| `scale`, `heal`, `migrate` | none in ProvMnS (these are NFV lifecycle, not NRM provisioning) | Deferred for future implementation |
| `monitor` | none (ProvMnS answers synchronously) | Proposed: a `monitor` completes immediately |

### `ResourceFunction` fields

| TMF664 field | 3GPP target | Rule | Status |
|---|---|---|---|
| `id` | MOI `id` in `/{className}={id}` | One-to-one | Proposed |
| `functionType` | IOC className: one of the TS28541 `*Function` IOCs (schemas `AmfFunction-Single` … `AnLFFunction-Single`, 39 in `TS28541_5GcNrm.yaml`) or `NetworkSlice` / `NetworkSliceSubnet` (`TS28541_SliceNrm.yaml`) | The className *string* in the ProvMnS path is not yet confirmed against the TS 28.541 text (the YAML gives schema names) | **TODO-B2**: confirm the exact className strings |
| `name` | `userLabel` (`ManagedFunction-Attr`, TS28623_GenericNrm) | | Proposed |
| `priority` | `priorityLabel` (`ManagedFunction-Attr`, integer) | Both are integers; the semantics differ (contention vs. label) | **TODO-B3** |
| `administrativeState` `locked` / `unlocked` | `AdministrativeState` `LOCKED` / `UNLOCKED` (TS28623_ComDefs) | Case mapping. Present on `NetworkSlice`, `NetworkSliceSubnet`, the `NFService` entries and `SnssaiInfo`. **Not** on the `*Function` IOCs themselves | **TODO-B4**: for an NF, which attribute does `administrativeState` drive, if any? |
| `administrativeState` `shutdown` | none: the R19 `AdministrativeState` enum has only `LOCKED` and `UNLOCKED` | Refuse with 400 | **TODO-B5** |
| `operationalState` `enable` / `disable` | `OperationalState` `ENABLED` / `DISABLED` (readOnly in 3GPP) | Read-only reflection; a TMF664 write is refused | Proposed |
| `usageState` `idle` / `active` / `busy` | `UsageState` `IDLE` / `ACTIVE` / `BUSY` (readOnly; on `NFService`) | Read-only reflection | Proposed (depends on F1: `usageState` is writable in the chosen file's `_Update`) |
| `resourceStatus` (`standby, alarm, available, reserved, unknown, suspended`) | `AvailabilityStatus` (`IN_TEST, FAILED, POWER_OFF, OFF_LINE, OFF_DUTY, DEPENDENCY, DEGRADED, NOT_INSTALLED, LOG_FULL`) | The enums do not correspond | **TODO-B6**: map, or leave unmapped? |
| `resourceCharacteristic` | IOC attributes; for slices `serviceProfileList` (`NetworkSlice`) and `sliceProfileList` (`NetworkSliceSubnet`) | Name = attribute name; structured values (ServiceProfile, SliceProfile) do not fit a flat name/value | **TODO-B7**: encoding of structured attributes |
| `resourceRelationship`, `connectivity` | `managedFunctionRefList`, `networkSliceSubnetRefList` (`NetworkSliceSubnet`), `networkSliceSubnetRef` (`NetworkSlice`) | DN references | Proposed for slices |
| `connectionPoint` | `EP_*` IOCs | | **TODO-B8** |
| `resourceSpecification` | none fixed | | **TODO-B9**: IOC class, or vendor model? |
| `resourceVersion` | `swVersion` is on `ManagedElement`, not on `ManagedFunction` | | **TODO-B10** |
| `category`, `description`, `value`, `role`, `startOperatingDate`, `endOperatingDate`, `place`, `relatedParty`, `note`, `attachment`, `activationFeature`, `autoModification`, `schedule` | none | Kept by TMF664 only | Proposed |

## Not done in this pass

- No code and no generated DTOs.
- No conformance of the TMF swagger itself checked beyond field names, types and enums.
- 3GPP barring (A4) and ProvMnS className strings (B2) still need a spec lookup before an answer
  is proposed.
