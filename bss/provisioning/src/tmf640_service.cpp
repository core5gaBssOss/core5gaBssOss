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

} // namespace provisioning::tmf640
