#include "expiry.hpp"

#include "sbi_core/datetime.hpp"
#include "sbi_core/store_quota.hpp"

#include <optional>
#include <string>

namespace nef {

namespace {

using json = nlohmann::json;

// The DateTime at `pointer`, when present, a string and parseable.
std::optional<Clock::time_point> date_at(const json& doc, const json::json_pointer& pointer) {
    if (!doc.is_object() || !doc.contains(pointer) || !doc.at(pointer).is_string()) {
        return std::nullopt;
    }
    return sbi_core::parse_rfc3339(doc.at(pointer).get<std::string>());
}

bool passed(const json& doc, const json::json_pointer& pointer, Clock::time_point now) {
    const auto at = date_at(doc, pointer);
    return at && *at <= now;
}

SweepAction remove_if_passed(const json& doc, const char* pointer, Clock::time_point now) {
    return passed(doc, json::json_pointer(pointer), now) ? SweepAction::remove : SweepAction::keep;
}

} // namespace

SweepAction expire_monitoring(const json& doc, Clock::time_point now) {
    return remove_if_passed(doc, "/monitorExpireTime", now);
}

SweepAction expire_event_exposure(const json& doc, Clock::time_point now) {
    return remove_if_passed(doc, "/eventsRepInfo/monDur", now);
}

SweepAction expire_time_sync(const json& doc, Clock::time_point now) {
    return remove_if_passed(doc, "/expiry", now);
}

SweepAction expire_np_configuration(const json& doc, Clock::time_point now) {
    return remove_if_passed(doc, "/validityTime", now);
}

SweepAction expire_5glan_np_config(json& doc, Clock::time_point now) {
    if (!passed(doc, json::json_pointer("/5gLanParams/npConfigParams/validityTime"), now)) {
        return SweepAction::keep;
    }
    doc.at("5gLanParams").erase("npConfigParams");
    return SweepAction::rewrite;
}

SweepAction expire_cp_parameter_sets(json& doc, Clock::time_point now) {
    if (!doc.is_object()) {
        return SweepAction::keep;
    }
    if (!doc.contains("cpParameterSets")) {
        return remove_if_passed(doc, "/validityTime", now);
    }
    auto& sets = doc.at("cpParameterSets");
    if (!sets.is_array()) {
        return SweepAction::keep;
    }
    json kept = json::array();
    for (const auto& set : sets) {
        if (!passed(set, json::json_pointer("/validityTime"), now)) {
            kept.push_back(set);
        }
    }
    if (kept.size() == sets.size()) {
        return SweepAction::keep;
    }
    if (kept.empty()) {
        return SweepAction::remove;
    }
    sets = std::move(kept);
    return SweepAction::rewrite;
}

void confirm_proposed_expiry(json& doc,
                             const json::json_pointer& pointer,
                             Clock::time_point now,
                             std::chrono::seconds max_lifetime) {
    const auto proposed = date_at(doc, pointer);
    if (!proposed) {
        return;
    }
    doc[pointer] = sbi_core::format_rfc3339(sbi_core::confirm_expiry(proposed, now, max_lifetime));
}

} // namespace nef
