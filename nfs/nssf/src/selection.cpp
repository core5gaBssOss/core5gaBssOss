#include "selection.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace nssf {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool snssai_equal(const sbi_gen::Snssai& a, const sbi_gen::Snssai& b) {
    return a.sst == b.sst && lower(a.sd.value_or("")) == lower(b.sd.value_or(""));
}

bool tai_equal_json(const nlohmann::json& j, const sbi_gen::Tai_CommonData& tai) {
    try {
        const auto other = j.get<sbi_gen::Tai_CommonData>();
        return other.plmnId.mcc == tai.plmnId.mcc && other.plmnId.mnc == tai.plmnId.mnc &&
               lower(other.tac) == lower(tai.tac);
    } catch (const nlohmann::json::exception&) {
        return false;
    }
}

bool in_list(const std::vector<sbi_gen::Snssai>& list, const sbi_gen::Snssai& s) {
    return std::any_of(list.begin(), list.end(), [&](const auto& c) { return snssai_equal(c, s); });
}

bool ta_allows(const std::optional<std::vector<nlohmann::json>>& ta_set, const sbi_gen::Snssai& s) {
    if (!ta_set.has_value()) {
        return true; // no TA-level information for this TAI: catalog decides alone
    }
    return std::any_of(ta_set->begin(), ta_set->end(), [&](const auto& ext) {
        return ext_snssai_matches(ext, s);
    });
}

} // namespace

bool ext_snssai_matches(const nlohmann::json& ext, const sbi_gen::Snssai& snssai) {
    if (!ext.is_object() || !ext.contains("sst") || !ext["sst"].is_number_integer() ||
        ext["sst"].get<std::int64_t>() != snssai.sst) {
        return false;
    }
    if (ext.value("wildcardSd", false)) {
        return true;
    }
    const std::string sd = lower(snssai.sd.value_or(""));
    if (auto it = ext.find("sdRanges"); it != ext.end() && it->is_array()) {
        if (sd.empty()) {
            return false;
        }
        // Six hex digits each (SdRange pattern), so lowercase string order is numeric order.
        return std::any_of(it->begin(), it->end(), [&](const nlohmann::json& range) {
            if (!range.is_object() || !range.contains("start") || !range.contains("end") ||
                !range["start"].is_string() || !range["end"].is_string()) {
                return false;
            }
            return lower(range["start"].get<std::string>()) <= sd &&
                   sd <= lower(range["end"].get<std::string>());
        });
    }
    const std::string ext_sd =
        ext.contains("sd") && ext["sd"].is_string() ? lower(ext["sd"].get<std::string>()) : "";
    return ext_sd == sd;
}

std::optional<std::vector<nlohmann::json>>
ta_supported_snssais(const std::vector<nlohmann::json>& availability_infos,
                     const sbi_gen::Tai_CommonData& tai) {
    std::optional<std::vector<nlohmann::json>> out;
    for (const auto& info : availability_infos) {
        auto data = info.find("supportedNssaiAvailabilityData");
        if (data == info.end() || !data->is_array()) {
            continue;
        }
        for (const auto& entry : *data) {
            if (!entry.is_object()) {
                continue;
            }
            bool covers = entry.contains("tai") && tai_equal_json(entry["tai"], tai);
            if (!covers && entry.contains("taiList") && entry["taiList"].is_array()) {
                covers =
                    std::any_of(entry["taiList"].begin(),
                                entry["taiList"].end(),
                                [&](const nlohmann::json& t) { return tai_equal_json(t, tai); });
            }
            if (!covers) {
                continue;
            }
            if (!out.has_value()) {
                out.emplace();
            }
            if (auto list = entry.find("supportedSnssaiList");
                list != entry.end() && list->is_array()) {
                out->insert(out->end(), list->begin(), list->end());
            }
        }
    }
    return out;
}

std::optional<sbi_gen::AuthorizedNetworkSliceInfo>
decide_slice_selection(const std::vector<sbi_gen::Snssai>& catalog,
                       const SelectionRequest& request,
                       const std::vector<nlohmann::json>& availability_infos,
                       const std::string& nrf_disc_api_uri) {
    std::vector<sbi_gen::Snssai> subscribed;
    if (request.subscribed.has_value()) {
        for (const auto& s : *request.subscribed) {
            subscribed.push_back(s.subscribedSnssai);
        }
    }
    const std::vector<sbi_gen::Snssai>& candidates = request.requested.has_value()
                                                         ? *request.requested
                                                     : request.subscribed.has_value() ? subscribed
                                                                                      : catalog;

    std::optional<std::vector<nlohmann::json>> ta_set;
    if (request.tai.has_value()) {
        ta_set = ta_supported_snssais(availability_infos, *request.tai);
    }

    std::vector<sbi_gen::Snssai> allowed;
    std::vector<sbi_gen::Snssai> rejected_plmn;
    std::vector<sbi_gen::Snssai> rejected_ta;
    for (const auto& c : candidates) {
        if (!in_list(catalog, c)) {
            rejected_plmn.push_back(c);
        } else if (!ta_allows(ta_set, c)) {
            rejected_ta.push_back(c);
        } else {
            allowed.push_back(c);
        }
    }

    // Registration: none of the Requested NSSAI is permitted -> the subscribed S-NSSAIs marked
    // default that are available in the current TA (TS 29.531 Table 6.1.6.2.2-1 allowedNssaiList,
    // third condition).
    if (request.procedure == SelectionProcedure::Registration && allowed.empty() &&
        request.requested.has_value() && request.subscribed.has_value()) {
        for (const auto& s : *request.subscribed) {
            if (s.defaultIndication.value_or(false) && in_list(catalog, s.subscribedSnssai) &&
                ta_allows(ta_set, s.subscribedSnssai) && !in_list(allowed, s.subscribedSnssai)) {
                allowed.push_back(s.subscribedSnssai);
            }
        }
    }

    sbi_gen::AuthorizedNetworkSliceInfo result;

    if (request.procedure == SelectionProcedure::PduSession) {
        // 5.2.2.2.3 step 2a: at least the NRF to select NFs within the slice (nsiInformation).
        if (allowed.empty()) {
            return std::nullopt;
        }
        sbi_gen::NsiInformation nsi;
        nsi.nrfId = nrf_disc_api_uri;
        result.nsiInformation = std::move(nsi);
        return result;
    }

    // configuredNssai (Table 6.1.6.2.2-1): registration without Requested NSSAI, or with one that
    // has an S-NSSAI not valid in the serving PLMN, or defaultConfiguredSnssaiInd; and always for
    // UE configuration update. The authorized configured S-NSSAIs: the subscribed ones the PLMN
    // supports, or the whole catalog when no subscription was given.
    const bool want_configured = request.procedure == SelectionProcedure::UeConfigurationUpdate ||
                                 (request.procedure == SelectionProcedure::Registration &&
                                  (!request.requested.has_value() || !rejected_plmn.empty()));
    std::vector<sbi_gen::ConfiguredSnssai> configured;
    if (want_configured) {
        const auto& base = request.subscribed.has_value() ? subscribed : catalog;
        for (const auto& s : base) {
            if (in_list(catalog, s)) {
                sbi_gen::ConfiguredSnssai cs;
                cs.configuredSnssai = s;
                configured.push_back(std::move(cs));
            }
        }
    }

    if (allowed.empty() && configured.empty()) {
        return std::nullopt;
    }

    if (!allowed.empty()) {
        std::vector<sbi_gen::AllowedSnssai> list;
        for (auto& s : allowed) {
            sbi_gen::AllowedSnssai a;
            a.allowedSnssai = s;
            list.push_back(std::move(a));
        }
        sbi_gen::AllowedNssai an;
        an.allowedSnssaiList = std::move(list);
        an.accessType.value = sbi_gen::AccessType::V3GPP_ACCESS;
        result.allowedNssaiList = std::vector<sbi_gen::AllowedNssai>{std::move(an)};
    }
    if (!configured.empty()) {
        result.configuredNssai = std::move(configured);
    }
    if (!rejected_plmn.empty()) {
        result.rejectedNssaiInPlmn = std::move(rejected_plmn);
    }
    if (!rejected_ta.empty()) {
        result.rejectedNssaiInTa = std::move(rejected_ta);
    }
    return result;
}

} // namespace nssf
