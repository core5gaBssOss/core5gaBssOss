#include "token_authz.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "discovery_authz.hpp"

namespace nrf {
namespace {

std::string lower(std::string s) {
    std::ranges::transform(
        s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::optional<std::string> field_of(const std::map<std::string, std::string>& fields,
                                    const char* key) {
    const auto it = fields.find(key);
    return it == fields.end() ? std::nullopt : std::optional{it->second};
}

TokenDecision refuse(std::string error, std::string description) {
    return TokenDecision{.error = std::move(error), .description = std::move(description)};
}

} // namespace

TokenDecision check_token_request_form(const std::map<std::string, std::string>& fields) {
    const auto field = [&](const char* k) { return field_of(fields, k); };
    const auto grant = field("grant_type");
    if (!grant) {
        return refuse("invalid_request", "grant_type is missing");
    }
    if (*grant != "client_credentials") {
        return refuse("unsupported_grant_type", "only client_credentials is supported");
    }
    const auto instance_id = field("nfInstanceId");
    const auto scope = field("scope");
    if (!instance_id || !scope || scope->empty()) {
        return refuse("invalid_request", "AccessTokenReq requires nfInstanceId and scope");
    }
    return {};
}

TokenDecision authorize_token_request(
    const std::map<std::string, std::string>& fields,
    const TokenRequester& requester,
    const std::function<std::optional<nlohmann::json>(const std::string&)>& get_profile,
    const std::function<std::vector<nlohmann::json>(const std::string&)>& producers_of) {
    if (auto d = check_token_request_form(fields); !d.error.empty()) {
        return d;
    }
    const auto field = [&](const char* k) { return field_of(fields, k); };
    const auto instance_id = field("nfInstanceId");
    const auto scope = field("scope");
    const auto profile = get_profile(*instance_id);
    if (!profile || !profile->contains("nfType") || !profile->at("nfType").is_string()) {
        return refuse("invalid_client", "the requesting NF instance is not registered");
    }
    const auto consumer_type = profile->at("nfType").get<std::string>();
    if (const auto t = field("nfType"); t && *t != consumer_type) {
        return refuse("invalid_client", "nfType does not match the registered NF profile");
    }
    // The certificate names the type as the NF itself is named: `5G_EIR` (TS 29.510 NFType) is the
    // `eir` of CN=eir / DNS:eir (ADR-0505); the full lower-cased type is accepted as well.
    const auto type_name = lower(consumer_type);
    const auto short_name = type_name.starts_with("5g_") ? type_name.substr(3) : type_name;
    const auto names_type = [&](const std::string& n) {
        const auto l = lower(n);
        return l == type_name || l == short_name;
    };
    const bool cert_names_type =
        names_type(requester.cert_cn) || std::ranges::any_of(requester.cert_dns_names, names_type);
    if (!cert_names_type) {
        return refuse("invalid_client", "the client certificate does not name the NF type");
    }

    const auto target_type = field("targetNfType").value_or("NRF");
    if (target_type == "NRF") {
        return {};
    }
    DiscoveryRequester who;
    who.nf_type = consumer_type;
    who.plmns = profile->value("plmnList", nlohmann::json());
    who.snssais = profile->value("sNssais", nlohmann::json());
    if (profile->contains("fqdn") && profile->at("fqdn").is_string()) {
        who.fqdn = profile->at("fqdn").get<std::string>();
    }
    const auto producers = producers_of(target_type);
    std::istringstream words(*scope);
    for (std::string s; words >> s;) {
        if (s.find(':') != std::string::npos) {
            continue; // resource/operation scope: not authorized separately (ADR-0499)
        }
        const bool ok = std::ranges::any_of(
            producers, [&](const nlohmann::json& p) { return service_authorized_for(p, s, who); });
        if (!ok) {
            return refuse("invalid_scope",
                          "no registered " + target_type + " offers " + s + " to a " +
                              consumer_type);
        }
    }
    return {};
}

nlohmann::json access_token_err(const TokenDecision& d) {
    return nlohmann::json{{"error", d.error}, {"error_description", d.description}};
}

} // namespace nrf
