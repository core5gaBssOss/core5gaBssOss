#include "tmf640_service.hpp"

#include "sbi_core/uuid.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <pqxx/pqxx>
#include <regex>
#include <set>

#include "TMF640_ServiceActivation_v4_0_0.hpp"

namespace provisioning::tmf640 {

using nlohmann::json;

namespace {

tl::unexpected<ServiceError> bad(const std::string& detail) {
    return tl::unexpected(ServiceError{400, "Bad Request", detail});
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

// ADR-0558 A3: SIM keys never enter a Service, because GET /service/{id} returns every
// characteristic. A deny-list of the names a client could plausibly use (the TS29505
// AuthenticationSubscription member names and the usual short forms); this is a guard, not a
// proof that no key can be smuggled under another name.
bool credential_name(const std::string& name) {
    static const std::set<std::string> kNames = {"k",
                                                 "ki",
                                                 "opc",
                                                 "op",
                                                 "sqn",
                                                 "encpermanentkey",
                                                 "encopckey",
                                                 "encopkey",
                                                 "sequencenumber",
                                                 "authenticationsubscription"};
    return kNames.contains(lower(name));
}

const std::set<std::string>& service_states() {
    // ServiceStateType (TMF640 swagger). The generated type accepts any string (ADR-0568), so the
    // closed set is enforced here.
    static const std::set<std::string> kStates = {tmf_gen::ServiceStateType::feasibilityChecked,
                                                  tmf_gen::ServiceStateType::designed,
                                                  tmf_gen::ServiceStateType::reserved,
                                                  tmf_gen::ServiceStateType::inactive,
                                                  tmf_gen::ServiceStateType::active,
                                                  tmf_gen::ServiceStateType::terminated};
    return kStates;
}

} // namespace

tl::expected<ValidatedService, ServiceError>
validate_create(const std::string& body,
                const std::function<bool(const std::string&)>& has_profile) {
    // Imsi-form Supi (TS29571_CommonData.yaml Supi admits more forms; the UDR OAM API takes this
    // one, ADR-0382) and Msisdn (TS29571_CommonData.yaml: ^[0-9]{5,15}$).
    static const std::regex kImsiSupi("^imsi-[0-9]{5,15}$");
    static const std::regex kMsisdn("^[0-9]{5,15}$");

    json raw;
    try {
        raw = json::parse(body);
    } catch (const json::parse_error&) {
        return bad("request body is not valid JSON");
    }
    if (!raw.is_object()) {
        return bad("request body must be a Service_Create object");
    }

    tmf_gen::Service_Create create;
    try {
        create = raw.get<tmf_gen::Service_Create>();
    } catch (const json::exception& e) {
        // Service_Create requires state and serviceSpecification (TMF640 swagger).
        return bad(std::string("not a valid Service_Create: ") + e.what());
    }

    ValidatedService v;
    v.state = create.state.value;
    if (!service_states().contains(v.state)) {
        return bad("state must be a ServiceStateType value");
    }
    if (v.state == tmf_gen::ServiceStateType::terminated) {
        return bad("state 'terminated' is not accepted: deprovisioning is deferred for future "
                   "implementation (ADR-0558 A5)");
    }
    if (create.category.value_or("") != "RFS") {
        return bad("category must be \"RFS\" (ADR-0558 A7)");
    }

    v.specification_id = create.serviceSpecification.id;
    if (v.specification_id.empty() || !has_profile(v.specification_id)) {
        return bad("serviceSpecification.id names no configured network profile (ADR-0558 A1)");
    }

    std::set<std::string> seen;
    for (const auto& c : raw.value("serviceCharacteristic", json::array())) {
        const auto name = c.value("name", std::string());
        if (credential_name(name)) {
            return bad("serviceCharacteristic '" + name +
                       "' is refused: SIM credentials never enter a Service (ADR-0558 A3)");
        }
        if (!seen.insert(name).second) {
            return bad("serviceCharacteristic '" + name + "' appears more than once");
        }
        const json value = c.value("value", json());
        if (name == "supi") {
            if (!value.is_string() || !std::regex_match(value.get<std::string>(), kImsiSupi)) {
                return bad("serviceCharacteristic 'supi' must be an IMSI-form SUPI string");
            }
            v.supi = value.get<std::string>();
        } else if (name == "msisdn") {
            if (!value.is_string() || !std::regex_match(value.get<std::string>(), kMsisdn)) {
                return bad("serviceCharacteristic 'msisdn' must be 5 to 15 digits");
            }
            v.msisdn = value.get<std::string>();
        }
    }
    if (v.supi.empty()) {
        return bad("serviceCharacteristic 'supi' is required (ADR-0558 A2)");
    }

    v.service = json(create);
    return v;
}

tl::expected<PatchPlan, ServiceError> plan_patch(const json& stored, const std::string& body) {
    static const std::regex kMsisdn("^[0-9]{5,15}$");

    json patch;
    try {
        patch = json::parse(body);
    } catch (const json::parse_error&) {
        return bad("request body is not valid JSON");
    }
    if (!patch.is_object()) {
        return bad("request body must be a Service_Update object");
    }
    for (const char* immutable : {"id", "href"}) {
        if (patch.contains(immutable)) {
            return bad(std::string(immutable) + " cannot be changed");
        }
    }

    json merged = stored;
    merged.merge_patch(patch); // RFC 7396; null removes a member
    PatchPlan plan;
    try {
        plan.service = json(merged.get<tmf_gen::Service>());
    } catch (const json::exception& e) {
        return bad(std::string("the patched object is not a valid Service: ") + e.what());
    }

    const auto field_or_empty = [](const json& j, const char* k) {
        return j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : std::string();
    };
    if (field_or_empty(plan.service, "category") != "RFS") {
        return bad("category must stay \"RFS\" (ADR-0558 A7)");
    }
    plan.specification_id = plan.service.at("serviceSpecification").value("id", std::string());
    if (plan.specification_id != stored.at("serviceSpecification").value("id", std::string())) {
        return bad("serviceSpecification.id cannot be changed");
    }

    std::set<std::string> seen;
    std::string supi_before, msisdn_before;
    for (const auto& c : stored.value("serviceCharacteristic", json::array())) {
        if (c.value("name", std::string()) == "supi") {
            supi_before = c.value("value", std::string());
        } else if (c.value("name", std::string()) == "msisdn") {
            msisdn_before = c.value("value", std::string());
        }
    }
    for (const auto& c : plan.service.value("serviceCharacteristic", json::array())) {
        const auto name = c.value("name", std::string());
        if (credential_name(name)) {
            return bad("serviceCharacteristic '" + name +
                       "' is refused: SIM credentials never enter a Service (ADR-0558 A3)");
        }
        if (!seen.insert(name).second) {
            return bad("serviceCharacteristic '" + name + "' appears more than once");
        }
        const json value = c.value("value", json());
        if (name == "supi") {
            plan.supi = value.is_string() ? value.get<std::string>() : std::string();
        } else if (name == "msisdn") {
            if (!value.is_string() || !std::regex_match(value.get<std::string>(), kMsisdn)) {
                return bad("serviceCharacteristic 'msisdn' must be 5 to 15 digits");
            }
            plan.msisdn = value.get<std::string>();
        }
    }
    if (plan.supi != supi_before || plan.msisdn != msisdn_before) {
        return bad("serviceCharacteristic 'supi' and 'msisdn' cannot be changed; create a new "
                   "Service instead");
    }

    const std::string before = stored.value("state", std::string());
    plan.state = plan.service.value("state", std::string());
    if (!service_states().contains(plan.state)) {
        return bad("state must be a ServiceStateType value");
    }
    if (plan.state == tmf_gen::ServiceStateType::terminated) {
        return bad("state 'terminated' is not accepted: deprovisioning is deferred for future "
                   "implementation (ADR-0558 A5)");
    }
    const bool was_live = before == tmf_gen::ServiceStateType::active ||
                          before == tmf_gen::ServiceStateType::inactive;
    if (plan.state == before) {
        plan.action = NetworkAction::None;
    } else if (plan.state == tmf_gen::ServiceStateType::active) {
        plan.action = NetworkAction::Activate;
    } else if (plan.state == tmf_gen::ServiceStateType::inactive) {
        plan.action =
            before == tmf_gen::ServiceStateType::active ? NetworkAction::Bar : NetworkAction::None;
    } else if (was_live) {
        return bad("a Service that was active or inactive cannot return to '" + plan.state +
                   "': the subscriber is already at the UDR");
    }
    return plan;
}

ServiceStore::ServiceStore(const std::string& orchestration_conninfo, std::size_t pool_size)
    : pool_(std::make_unique<nf_config::PgPool>(orchestration_conninfo, pool_size)) {}

tl::expected<json, ServiceError>
ServiceStore::create(const ValidatedService& v,
                     const std::function<tl::expected<void, ServiceError>()>& network_write) {
    const std::string id = sbi_core::generate_uuid_v4();
    json service = v.service;
    service["id"] = id;
    service["href"] = std::string(kApiRoot) + "/service/" + id;
    // Normalise through the generated Service DTO, the shape GET returns.
    service = json(service.get<tmf_gen::Service>());

    auto lease = pool_->acquire();
    pqxx::work txn(lease.conn());
    const auto inserted = txn.exec(
        "INSERT INTO orchestration.tmf640_service "
        "(id, ue_id, state, service_specification_id, body) VALUES ($1, $2, $3, $4, $5::jsonb) "
        "ON CONFLICT (ue_id) DO NOTHING RETURNING id",
        pqxx::params{id, v.supi, v.state, v.specification_id, service.dump()});
    if (inserted.empty()) {
        return tl::unexpected(
            ServiceError{409, "Conflict", "a Service already exists for this supi"});
    }
    if (auto written = network_write(); !written.has_value()) {
        return tl::unexpected(written.error()); // txn rolls back: nothing stored
    }
    txn.commit();
    return service;
}

tl::expected<json, ServiceError> ServiceStore::patch(
    const std::string& id,
    const std::function<tl::expected<json, ServiceError>(const json&)>& transition) {
    auto lease = pool_->acquire();
    pqxx::work txn(lease.conn());
    const auto rows = txn.exec(
        "SELECT body FROM orchestration.tmf640_service WHERE id = $1 FOR UPDATE", pqxx::params{id});
    if (rows.empty()) {
        return tl::unexpected(ServiceError{404, "Not Found", "no Service with this id"});
    }
    auto next = transition(json::parse(rows[0][0].c_str()));
    if (!next.has_value()) {
        return tl::unexpected(next.error()); // txn rolls back: nothing stored
    }
    txn.exec("UPDATE orchestration.tmf640_service SET state = $2, body = $3::jsonb, "
             "updated_at = now() WHERE id = $1",
             pqxx::params{id, next->value("state", std::string()), next->dump()});
    txn.commit();
    return *next;
}

std::optional<json> ServiceStore::get(const std::string& id) {
    auto lease = pool_->acquire();
    pqxx::read_transaction txn(lease.conn());
    const auto rows =
        txn.exec("SELECT body FROM orchestration.tmf640_service WHERE id = $1", pqxx::params{id});
    if (rows.empty()) {
        return std::nullopt;
    }
    return std::optional<json>(std::in_place, json::parse(rows[0][0].c_str()));
}

ServiceStore::Page ServiceStore::list(std::size_t offset, std::size_t limit) {
    auto lease = pool_->acquire();
    pqxx::read_transaction txn(lease.conn());
    Page page;
    page.total = txn.exec("SELECT count(*) FROM orchestration.tmf640_service")
                     .one_row()[0]
                     .as<std::size_t>();
    const auto rows =
        txn.exec("SELECT body FROM orchestration.tmf640_service "
                 "ORDER BY created_at, id OFFSET $1 LIMIT $2",
                 pqxx::params{static_cast<long long>(offset), static_cast<long long>(limit)});
    for (const auto& r : rows) {
        page.items.push_back(json::parse(r[0].c_str()));
    }
    return page;
}

tl::expected<json, ServiceError>
create_service(ServiceStore& store, UdrAdapter& udr, const ValidatedService& v) {
    return store.create(v, [&]() -> tl::expected<void, ServiceError> {
        // ADR-0558: feasibilityChecked/designed/reserved/inactive write nothing to the network in
        // increment 1 (inactive's barring is P3).
        if (v.state != tmf_gen::ServiceStateType::active) {
            return {};
        }
        auto written = udr.provision_network(v.supi, v.msisdn, v.specification_id);
        if (written.has_value()) {
            return {};
        }
        if (written.error().no_credentials) {
            return tl::unexpected(ServiceError{
                409,
                "Conflict",
                "the subscriber has no credentials at the UDR yet; the order flow provisions "
                "them before activation (ADR-0569)"});
        }
        spdlog::warn(
            "provisioning: TMF640 activation of {} failed: {}", v.supi, written.error().message);
        return tl::unexpected(
            ServiceError{500, "Internal Server Error", "the UDR write failed; nothing was stored"});
    });
}

tl::expected<json, ServiceError> patch_service(ServiceStore& store,
                                               UdrAdapter& udr,
                                               const std::string& id,
                                               const std::string& body) {
    return store.patch(id, [&](const json& stored) -> tl::expected<json, ServiceError> {
        auto plan = plan_patch(stored, body);
        if (!plan.has_value()) {
            return tl::unexpected(plan.error());
        }
        if (plan->action != NetworkAction::None) {
            auto written = udr.provision_network(plan->supi,
                                                 plan->msisdn,
                                                 plan->specification_id,
                                                 plan->action == NetworkAction::Bar);
            if (!written.has_value()) {
                if (written.error().no_credentials) {
                    return tl::unexpected(ServiceError{
                        409,
                        "Conflict",
                        "the subscriber has no credentials at the UDR yet; the order flow "
                        "provisions them before activation (ADR-0569)"});
                }
                spdlog::warn("provisioning: TMF640 PATCH of {} failed at the UDR: {}",
                             plan->supi,
                             written.error().message);
                return tl::unexpected(ServiceError{
                    500, "Internal Server Error", "the UDR write failed; nothing was stored"});
            }
        }
        return plan->service;
    });
}

} // namespace provisioning::tmf640
