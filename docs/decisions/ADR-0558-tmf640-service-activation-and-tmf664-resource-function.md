## ADR-0558: TMF640 Service Activation and TMF664 Resource Function Activation for BSS-to-network provisioning

**Date:** 2026-11-03. **Status:** Accepted.

**Origin.** From the reference-diagram review (TM Forum Open API ring). Third in the
user-directed implementation order (after ADR-0556 and ADR-0557). See ADR-0556 "Origin".

**Context.** `bss/provisioning` runs the order saga TMF622 -> TMF641 -> TMF652 (ADR-0382) and
then writes the subscriber's 3GPP documents straight to the UDR's project-owned OAM provisioning
API (`udr_adapter`). The step from a service order to "the service is active in the network" has
no TM Forum contract, so an ODA-aligned commercial stack could not drive it. TM Forum's standard
APIs for that step are TMF640 and TMF664.

**Sources (fetched, not recalled; ADR-0310 method).**
- `github.com/tmforum-apis/TMF640_ActivationConfiguration` @ `44d0e9e8`,
  `TMF640-ServiceActivation-v4.0.0.swagger.json`: title "API Service Activation and
  Configuration" 4.0.0, basePath `/tmf-api/ServiceActivationAndConfiguration/v4`, resources
  `service`, `monitor`, `hub` and the listener paths. `ServiceStateType`: feasibilityChecked,
  designed, reserved, inactive, active, terminated. Licence Apache-2.0.
- `github.com/tmforum-apis/TMF664_ResourceFunctionActivationConfiguration` @ `2f7bb576`,
  `TMF664-ResourceFunctionActivation-v4.0.0.swagger.json`: title "Resource Function Activation
  and Configuration" 4.0.0, basePath `/tmf-api/resourceFunctionActivation/v4/`, resources
  `resourceFunction`, `scale`, `heal`, `migrate`, `monitor`, `hub`. Licence Apache-2.0.
- Each repo holds a second v4.0.0 file (`..._Management_API_v4.0.0_swagger.json`). The
  implementation PR diffs the two and records which one the code is generated from.
- Number check from the diagram: its ring labels TMF645 "Appointment". Per the `tmforum-apis`
  repo names, TMF645 is ServiceQualification and TMF646 is AppointmentManagement.

**Decision:**
1. **TMF640** becomes the contract between the order saga and network activation. The TMF641
   step creates or patches a TMF640 `service`; its `state` moving to `active` (or the call
   failing) drives the saga. The existing UDR adapter becomes the TMF640 implementation's
   southbound for subscriber services. No behaviour toward the UDR changes.
2. **TMF664** `resourceFunction` represents network functions and slices as resources for
   activation, scale and heal requests from the BSS/OSS side. Its southbound is the 3GPP
   management plane (TS 28.532 provisioning MnS on the TS 28.541 NRM), not the SBI.
3. DTOs are generated from the fetched swagger, never hand-written. Notifications use the
   `hub`/listener pattern from the same files.
4. Before any mapping code, a reviewable table (in the style of `docs/CHARGING_MAPPING.md`)
   lists TMF640 `Service` and TMF664 `ResourceFunction` fields against the 3GPP data they drive.
   Ambiguous rows are marked TODO and asked about.
5. Increment order: TMF640 first (it slots into the existing saga), TMF664 second.

**Rejected alternatives.**
- Keep the direct saga-to-UDR call: works, but no standard contract for an ODA component swap.
- TMF645 Service Qualification now: useful for pre-order checks, lower value; deferred for
  future implementation.
- A project-specific activation API: a hand-written API where TM Forum publishes one.

**Disclosed.**
- Design only. No code exists.
- The TMF664 -> TS 28.541/28.532 mapping is not decided. The project has no provisioning MnS
  implementation today, so TMF664's southbound is deferred for future implementation until that
  mapping is asked and approved.
- TMF640 v4.0.0 is the latest version in the public `tmforum-apis` repo; whether a v5 exists
  elsewhere on TM Forum's site has not been checked.
