#pragma once

// TMF640 Service Activation and Configuration v4.0.0 (ADR-0558, ADR-0569, ADR-0576): POST
// /service, GET /service, GET /service/{id}, PATCH /service/{id}, DELETE /service/{id}. Request and
// response bodies are the generated tmf_gen DTOs
// (specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json, ADR-0568). The mapping onto 3GPP
// data is docs/PROVISIONING_MAPPING.md Part A, rows approved 2026-11-07:
//
//   A1  serviceSpecification.id is the network-profile key (config network_profiles), exact match.
//   A2  serviceCharacteristic "supi" (required, imsi- form) and "msisdn" (optional, digits).
//   A3  no SIM keys in a Service: credential-named characteristics are refused.
//   A4  state "inactive" bars the subscriber: amData.odbPacketServices = ALL_PACKET_SERVICES;
//       "active" clears it (null). Enforcement in the AMF/SMF is deferred (ADR-0576).
//   A5  state "terminated" is refused; a Service is removed with DELETE (ADR-0576).
//   A6  startDate/endDate are stored only.
//   A7  category must be "RFS".
//   Invariant (ADR-0576): the UDR holds the subscriber's network data if and only if the state is
//   "active" or "inactive". The pre-active states (feasibilityChecked, designed, reserved) write
//   nothing; see plan_patch for every transition.
//
// Not yet (deferred for future implementation): the `fields` query parameter, event
// notifications (P5, waiting on TMF630), /monitor.

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <nf_config/pg_pool.hpp>
#include <optional>
#include <string>
#include <tl/expected.hpp>
#include <vector>

#include "udr_adapter.hpp"

namespace provisioning::tmf640 {

inline constexpr const char* kApiRoot = "/tmf-api/ServiceActivationAndConfiguration/v4";

struct ServiceError {
    int status = 400;
    std::string title;
    std::string detail; // never echoes a characteristic value
};

// A POST /service body that passed every A-row check.
struct ValidatedService {
    nlohmann::json service; // the Service_Create, as normalised by the generated DTO
    std::string supi;
    std::string msisdn; // empty when absent
    std::string specification_id;
    std::string state;
};

// The A-row checks shared by a POST body and a merged PATCH result: closed state set without
// "terminated" (A5), category "RFS" (A7), a configured serviceSpecification.id (A1), no
// credential characteristic and no duplicate (A3), supi required and IMSI-form, msisdn digits
// (A2). Pure.
tl::expected<ValidatedService, ServiceError>
check_service(const nlohmann::json& raw,
              const std::function<bool(const std::string&)>& has_profile);

// Validates a POST /service body. `has_profile` answers whether a network profile exists for a
// ServiceSpecification id. Pure: no database, no network.
tl::expected<ValidatedService, ServiceError>
validate_create(const std::string& body,
                const std::function<bool(const std::string&)>& has_profile);

// ADR-0576: what one PATCH does at the UDR.
enum class UdrAction {
    None,      // store only
    FullWrite, // PUT .../subscribers/{supi}: the network documents (barred per `barred`)
    AmData,    // PUT .../subscribers/{supi}/am-data: odbPacketServices set or cleared
};

struct PatchPlan {
    nlohmann::json service; // the merged Service, normalised through the generated DTO
    ValidatedService validated;
    UdrAction action = UdrAction::None;
    bool barred = false;              // for FullWrite and AmData
    bool state_changed = false;       // for a ServiceStateChangeEvent
    std::vector<std::string> changed; // other top-level members that changed (fieldPath)
};

inline bool network_provisioned_state(const std::string& state) {
    return state == "active" || state == "inactive";
}

// Decides one PATCH /service/{id}: `stored` is the Service as GET returns it, `content_type` the
// request's, `body` the request body. The body is a JSON merge patch (RFC 7386) of
// Service_Update members only. 400 for a wrong content type (the TMF640 PATCH responses list no
// 415), a member Service_Update does not have, a merged Service check_service refuses, a changed
// supi or serviceSpecification.id, or a move from active/inactive back to a pre-active state.
// Pure: no database, no network.
tl::expected<PatchPlan, ServiceError>
plan_patch(const nlohmann::json& stored,
           const std::string& content_type,
           const std::string& body,
           const std::function<bool(const std::string&)>& has_profile);

// The orchestration-DB table orchestration.tmf640_service (deploy/db/orchestration/schema.sql).
class ServiceStore {
public:
    ServiceStore(const std::string& orchestration_conninfo, std::size_t pool_size);

    // Stores the Service with a new id and href. `network_write` runs inside the same
    // transaction, after the row is inserted (so a second Service for the same SUPI is refused
    // before any network write) and before commit (so a failed write stores nothing).
    tl::expected<nlohmann::json, ServiceError>
    create(const ValidatedService& v,
           const std::function<tl::expected<void, ServiceError>()>& network_write);

    std::optional<nlohmann::json> get(const std::string& id);

    // ADR-0576. Locks the row, runs `decide` on the stored Service, stores the result and runs
    // `network_write` before commit, so a refused patch or a failed write stores nothing. 404 if
    // the id is unknown.
    tl::expected<PatchPlan, ServiceError> update(
        const std::string& id,
        const std::function<tl::expected<PatchPlan, ServiceError>(const nlohmann::json&)>& decide,
        const std::function<tl::expected<void, ServiceError>(const PatchPlan&)>& network_write);

    // ADR-0576. Deletes the row and runs `network_write` (given the deleted Service) before
    // commit. Returns the deleted Service; 404 if the id is unknown.
    tl::expected<nlohmann::json, ServiceError>
    remove(const std::string& id,
           const std::function<tl::expected<void, ServiceError>(const ValidatedService&)>&
               network_write);

    struct Page {
        std::vector<nlohmann::json> items;
        std::size_t total = 0;
    };
    Page list(std::size_t offset, std::size_t limit);

private:
    std::unique_ptr<nf_config::PgPool> pool_;
};

// POST /service after validation: stores the Service and, for state "active", writes the
// network documents to the UDR inside the store's transaction (no credentials, ADR-0569). A UDR
// 409 (no credentials yet) is answered 409, any other UDR failure 500 (the TMF640 POST codes have
// no 502); either way nothing is stored.
tl::expected<nlohmann::json, ServiceError>
create_service(ServiceStore& store, UdrAdapter& udr, const ValidatedService& v);

// PATCH /service/{id}: plan_patch under the row lock, then the plan's UDR action.
tl::expected<PatchPlan, ServiceError> patch_service(ServiceStore& store,
                                                    UdrAdapter& udr,
                                                    const std::string& id,
                                                    const std::string& content_type,
                                                    const std::string& body);

// DELETE /service/{id}: for an active or inactive Service the subscriber is deprovisioned at the
// UDR (credentials included); a pre-active Service wrote nothing, so nothing is deleted there.
tl::expected<nlohmann::json, ServiceError>
delete_service(ServiceStore& store, UdrAdapter& udr, const std::string& id);

} // namespace provisioning::tmf640
