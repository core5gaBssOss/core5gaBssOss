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

// Maps a UDR write onto the TMF640 answer: a UDR 409 (no credentials yet) is 409, any other
// failure 500 (the TMF640 codes have no 502); either way the caller's transaction rolls back.
tl::expected<void, ServiceError>
udr_outcome(const tl::expected<UdrProvisionOutcome, UdrWriteError>& written,
            const std::string& supi) {
    if (written.has_value()) {
        return {};
    }
    if (written.error().no_credentials) {
        return tl::unexpected(
            ServiceError{409,
                         "Conflict",
                         "the subscriber has no credentials at the UDR yet; the order flow "
                         "provisions them before activation (ADR-0569)"});
    }
    spdlog::warn("provisioning: TMF640 UDR write for {} failed: {}", supi, written.error().message);
    return tl::unexpected(
        ServiceError{500, "Internal Server Error", "the UDR write failed; nothing was stored"});
}

} // namespace

tl::expected<ValidatedService, ServiceError>
check_service(const json& raw, const std::function<bool(const std::string&)>& has_profile) {
    // Imsi-form Supi (TS29571_CommonData.yaml Supi admits more forms; the UDR OAM API takes this
    // one, ADR-0382) and Msisdn (TS29571_CommonData.yaml: ^[0-9]{5,15}$).
    static const std::regex kImsiSupi("^imsi-[0-9]{5,15}$");
    static const std::regex kMsisdn("^[0-9]{5,15}$");

    ValidatedService v;
    const json state = raw.value("state", json());
    v.state = state.is_string() ? state.get<std::string>() : std::string();
    if (!service_states().contains(v.state)) {
        return bad("state must be a ServiceStateType value");
    }
    if (v.state == tmf_gen::ServiceStateType::terminated) {
        return bad("state 'terminated' is not accepted: a Service is removed with DELETE "
                   "(ADR-0558 A5, ADR-0576)");
    }
    const json category = raw.value("category", json());
    if (!category.is_string() || category.get<std::string>() != "RFS") {
        return bad("category must be \"RFS\" (ADR-0558 A7)");
    }

    const json spec = raw.value("serviceSpecification", json());
    const json spec_id = spec.is_object() ? spec.value("id", json()) : json();
    v.specification_id = spec_id.is_string() ? spec_id.get<std::string>() : std::string();
    if (v.specification_id.empty() || !has_profile(v.specification_id)) {
        return bad("serviceSpecification.id names no configured network profile (ADR-0558 A1)");
    }

    const json chars = raw.value("serviceCharacteristic", json::array());
    if (!chars.is_array()) {
        return bad("serviceCharacteristic must be an array");
    }
    std::set<std::string> seen;
    for (const auto& c : chars) {
        if (!c.is_object()) {
            return bad("every serviceCharacteristic must be an object");
        }
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
    v.service = raw;
    return v;
}

tl::expected<ValidatedService, ServiceError>
validate_create(const std::string& body,
                const std::function<bool(const std::string&)>& has_profile) {
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

    auto v = check_service(raw, has_profile);
    if (!v.has_value()) {
        return v;
    }
    v->service = json(create);
    return v;
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

tl::expected<PatchPlan, ServiceError> ServiceStore::update(
    const std::string& id,
    const std::function<tl::expected<PatchPlan, ServiceError>(const json&)>& decide,
    const std::function<tl::expected<void, ServiceError>(const PatchPlan&)>& network_write) {
    auto lease = pool_->acquire();
    pqxx::work txn(lease.conn());
    const auto rows = txn.exec(
        "SELECT body FROM orchestration.tmf640_service WHERE id = $1 FOR UPDATE", pqxx::params{id});
    if (rows.empty()) {
        return tl::unexpected(ServiceError{404, "Not Found", "no Service with this id"});
    }
    auto plan = decide(json::parse(rows[0][0].c_str()));
    if (!plan.has_value()) {
        return plan; // txn rolls back: nothing stored
    }
    txn.exec("UPDATE orchestration.tmf640_service SET state = $2, body = $3::jsonb, "
             "updated_at = now() WHERE id = $1",
             pqxx::params{id, plan->validated.state, plan->service.dump()});
    if (auto written = network_write(*plan); !written.has_value()) {
        return tl::unexpected(written.error());
    }
    txn.commit();
    return plan;
}

tl::expected<json, ServiceError> ServiceStore::remove(
    const std::string& id,
    const std::function<tl::expected<void, ServiceError>(const ValidatedService&)>& network_write) {
    auto lease = pool_->acquire();
    pqxx::work txn(lease.conn());
    const auto rows = txn.exec("DELETE FROM orchestration.tmf640_service WHERE id = $1 "
                               "RETURNING ue_id, state, body",
                               pqxx::params{id});
    if (rows.empty()) {
        return tl::unexpected(ServiceError{404, "Not Found", "no Service with this id"});
    }
    ValidatedService v;
    v.supi = rows[0][0].c_str();
    v.state = rows[0][1].c_str();
    v.service = json::parse(rows[0][2].c_str());
    if (auto written = network_write(v); !written.has_value()) {
        return tl::unexpected(written.error()); // txn rolls back: the Service is kept
    }
    txn.commit();
    return v.service;
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
        // ADR-0576 invariant: network data at the UDR iff active or inactive; inactive is barred
        // (A4). The pre-active states write nothing.
        if (!network_provisioned_state(v.state)) {
            return {};
        }
        const bool barred = v.state == tmf_gen::ServiceStateType::inactive;
        return udr_outcome(udr.provision_network(v.supi, v.msisdn, v.specification_id, barred),
                           v.supi);
    });
}

tl::expected<PatchPlan, ServiceError>
plan_patch(const json& stored,
           const std::string& content_type,
           const std::string& body,
           const std::function<bool(const std::string&)>& has_profile) {
    // The TMF640 swagger consumes application/json;charset=utf-8; parameters are ignored.
    std::string media = lower(content_type.substr(0, content_type.find(';')));
    media.erase(std::remove_if(media.begin(),
                               media.end(),
                               [](unsigned char c) { return std::isspace(c) != 0; }),
                media.end());
    if (media != "application/json") {
        return bad("content-type must be application/json (the TMF640 swagger's PATCH media type)");
    }
    json patch;
    try {
        patch = json::parse(body);
    } catch (const json::parse_error&) {
        return bad("request body is not valid JSON");
    }
    if (!patch.is_object()) {
        return bad("request body must be a Service_Update object");
    }
    // The Service_Update members (TMF640 swagger definitions/Service_Update).
    static const std::set<std::string> kUpdatable = {"category",
                                                     "description",
                                                     "endDate",
                                                     "hasStarted",
                                                     "isBundle",
                                                     "isServiceEnabled",
                                                     "isStateful",
                                                     "name",
                                                     "serviceType",
                                                     "startDate",
                                                     "startMode",
                                                     "feature",
                                                     "note",
                                                     "place",
                                                     "relatedEntity",
                                                     "relatedParty",
                                                     "serviceCharacteristic",
                                                     "serviceOrderItem",
                                                     "serviceRelationship",
                                                     "serviceSpecification",
                                                     "state",
                                                     "supportingResource",
                                                     "supportingService",
                                                     "@baseType",
                                                     "@schemaLocation",
                                                     "@type"};
    for (const auto& [key, _] : patch.items()) {
        if (!kUpdatable.contains(key)) {
            return bad("'" + key + "' is not a Service_Update member");
        }
    }

    auto before = check_service(stored, has_profile);
    if (!before.has_value()) {
        // A stored Service always passed these checks; a profile removed from config since then
        // is the one way to get here.
        return tl::unexpected(ServiceError{409,
                                           "Conflict",
                                           "the stored Service no longer passes validation: " +
                                               before.error().detail});
    }
    json merged = stored;
    merged.merge_patch(patch);
    try {
        merged = json(merged.get<tmf_gen::Service>());
    } catch (const json::exception& e) {
        return bad(std::string("the patched Service is not a valid Service: ") + e.what());
    }
    auto after = check_service(merged, has_profile);
    if (!after.has_value()) {
        return tl::unexpected(after.error());
    }
    if (after->supi != before->supi) {
        return bad("serviceCharacteristic 'supi' cannot change (a merge patch replaces the whole "
                   "serviceCharacteristic array, so it must carry supi unchanged)");
    }
    if (after->specification_id != before->specification_id) {
        return bad("serviceSpecification.id cannot change: re-provisioning to another network "
                   "profile is deferred for future implementation");
    }

    PatchPlan plan;
    plan.state_changed = after->state != before->state;
    const bool was = network_provisioned_state(before->state);
    const bool now = network_provisioned_state(after->state);
    const bool msisdn_changed = after->msisdn != before->msisdn;
    plan.barred = after->state == tmf_gen::ServiceStateType::inactive;
    if (was && !now) {
        return bad("state '" + before->state + "' cannot go back to '" + after->state +
                   "': the subscriber's network data is provisioned (ADR-0576)");
    }
    if (!now) {
        plan.action = UdrAction::None; // pre-active -> pre-active: store only
    } else if (!was || msisdn_changed) {
        plan.action = UdrAction::FullWrite; // first provisioning, or the documents change
    } else if (plan.state_changed) {
        plan.action = UdrAction::AmData; // active <-> inactive
    }
    for (const auto& [key, value] : merged.items()) {
        if (key != "state" && (!stored.contains(key) || stored.at(key) != value)) {
            plan.changed.push_back(key);
        }
    }
    for (const auto& [key, _] : stored.items()) {
        if (key != "state" && !merged.contains(key)) {
            plan.changed.push_back(key);
        }
    }
    plan.service = merged;
    plan.validated = std::move(*after);
    plan.validated.service = merged;
    return plan;
}

tl::expected<PatchPlan, ServiceError> patch_service(ServiceStore& store,
                                                    UdrAdapter& udr,
                                                    const std::string& id,
                                                    const std::string& content_type,
                                                    const std::string& body) {
    const auto has_profile = [&udr](const std::string& p) { return udr.has_profile(p); };
    return store.update(
        id,
        [&](const json& stored) { return plan_patch(stored, content_type, body, has_profile); },
        [&](const PatchPlan& plan) -> tl::expected<void, ServiceError> {
            const auto& v = plan.validated;
            switch (plan.action) {
                case UdrAction::None:
                    return {};
                case UdrAction::FullWrite:
                    return udr_outcome(
                        udr.provision_network(v.supi, v.msisdn, v.specification_id, plan.barred),
                        v.supi);
                case UdrAction::AmData:
                    return udr_outcome(
                        udr.provision_am_data(v.supi, v.msisdn, v.specification_id, plan.barred),
                        v.supi);
            }
            return {};
        });
}

tl::expected<json, ServiceError>
delete_service(ServiceStore& store, UdrAdapter& udr, const std::string& id) {
    return store.remove(id, [&](const ValidatedService& v) -> tl::expected<void, ServiceError> {
        if (!network_provisioned_state(v.state)) {
            return {}; // a pre-active Service wrote nothing at the UDR
        }
        if (auto gone = udr.deprovision(v.supi); !gone.has_value()) {
            spdlog::warn("provisioning: TMF640 deprovisioning of {} failed: {}",
                         v.supi,
                         gone.error().message);
            return tl::unexpected(ServiceError{
                500, "Internal Server Error", "the UDR delete failed; the Service is kept"});
        }
        return {};
    });
}

} // namespace provisioning::tmf640
