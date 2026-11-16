#pragma once

// TMF640 Service Activation and Configuration v4.0.0, increments 1 and 2 (ADR-0558, ADR-0569,
// ADR-0576): POST /service, GET /service, GET /service/{id}, PATCH /service/{id}. Request and
// response bodies are the generated tmf_gen DTOs
// (specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json, ADR-0568). The mapping onto 3GPP
// data is docs/PROVISIONING_MAPPING.md Part A, rows approved 2026-11-07:
//
//   A1  serviceSpecification.id is the network-profile key (config network_profiles), exact match.
//   A2  serviceCharacteristic "supi" (required, imsi- form) and "msisdn" (optional, digits).
//   A3  no SIM keys in a Service: credential-named characteristics are refused.
//   A5  state "terminated" is refused (the UDR OAM API cannot delete).
//   A6  startDate/endDate are stored only.
//   A7  category must be "RFS".
//   state "active" writes the network documents to the UDR (no credentials, ADR-0569); the other
//   pre-active states write nothing.
//
//   A4  PATCH: `inactive` writes amData.odbPacketServices = ALL_PACKET_SERVICES, `active` writes
//       the documents without it (ADR-0576). Enforcement in AMF/SMF is not built.
//   PATCH is RFC 7396 merge-patch.
//
// Not built (deferred for future implementation): DELETE, the `fields` query parameter, event
// notifications (P5), /monitor.

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

// Validates a POST /service body. `has_profile` answers whether a network profile exists for a
// ServiceSpecification id. Pure: no database, no network.
tl::expected<ValidatedService, ServiceError>
validate_create(const std::string& body,
                const std::function<bool(const std::string&)>& has_profile);

// What PATCH must do at the UDR (ADR-0576).
enum class NetworkAction {
    None,     // stored-only change, or a move between pre-active states
    Activate, // network documents without ODB (state -> active)
    Bar,      // network documents with odbPacketServices = ALL_PACKET_SERVICES (active -> inactive)
};

struct PatchPlan {
    nlohmann::json service; // the merged Service, as normalised by the generated DTO
    std::string supi;
    std::string msisdn;
    std::string specification_id;
    std::string state;
    NetworkAction action = NetworkAction::None;
};

// Applies an RFC 7396 merge-patch `body` to the `stored` Service and decides the network action.
// Refuses (400) a body that is not a JSON object, a change of id, href, category,
// serviceSpecification.id, supi or msisdn, a credential-named or duplicate characteristic, a state
// outside ServiceStateType, `terminated`, and a move from active/inactive back to a pre-active
// state. Pure: no database, no network.
tl::expected<PatchPlan, ServiceError> plan_patch(const nlohmann::json& stored,
                                                 const std::string& body);

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

    // PATCH: loads the Service under a row lock (404 if absent), hands it to `transition`, and
    // stores what it returns before commit, so a failed UDR write inside `transition` stores
    // nothing and two concurrent PATCHes of one Service run one after the other.
    tl::expected<nlohmann::json, ServiceError>
    patch(const std::string& id,
          const std::function<tl::expected<nlohmann::json, ServiceError>(const nlohmann::json&)>&
              transition);

    std::optional<nlohmann::json> get(const std::string& id);

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

// PATCH /service/{id}: plans the change, writes the UDR for state transitions, stores the merged
// Service. A UDR 409 is answered 409, any other UDR failure 500; either way nothing is stored.
tl::expected<nlohmann::json, ServiceError>
patch_service(ServiceStore& store, UdrAdapter& udr, const std::string& id, const std::string& body);

} // namespace provisioning::tmf640
