#include "nf_update.hpp"

#include "sbi_core/datetime.hpp"
#include "sbi_core/json_body.hpp"

#include <cstddef>

namespace nrf {

namespace {

using sbi_core::http2::problem_response;
using sbi_core::http2::Response;

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

} // namespace

std::string profile_etag(const nlohmann::json& profile) {
    return "\"" + std::to_string(std::hash<std::string>{}(profile.dump())) + "\"";
}

bool if_match_satisfied(std::string_view if_match, const std::string& current_etag) {
    if (trim(if_match) == "*") {
        return true;
    }
    while (!if_match.empty()) {
        const std::size_t comma = if_match.find(',');
        const std::string_view tag = trim(if_match.substr(0, comma));
        if (tag == current_etag) {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        if_match.remove_prefix(comma + 1);
    }
    return false;
}

tl::expected<nlohmann::json, Response>
patch_nf_profile(const nlohmann::json& current,
                 const nlohmann::json& ops,
                 const std::string& path_id,
                 const std::optional<std::string>& if_match,
                 const std::function<std::optional<std::string>(const nlohmann::json&)>& validate) {
    if (if_match.has_value() && !if_match_satisfied(*if_match, profile_etag(current))) {
        // TS 29.500 table 5.2.7.2-1 defines no 412 cause.
        return tl::unexpected(
            problem_response(412,
                             "Precondition Failed",
                             "If-Match does not match the NF profile's entity tag",
                             std::nullopt));
    }
    nlohmann::json patched;
    try {
        patched = current.patch(ops);
    } catch (const nlohmann::json::parse_error& e) {
        // nlohmann reports a PatchItem without "op"/"path", or with an unknown "op", this way.
        return tl::unexpected(
            problem_response(400, "Bad Request", e.what(), std::string("INVALID_MSG_FORMAT")));
    } catch (const nlohmann::json::exception& e) {
        // A path that does not exist, or a failed "test". TS 29.500 table 5.2.7.2-1 defines no 409
        // cause.
        return tl::unexpected(problem_response(409, "Conflict", e.what(), std::nullopt));
    }
    if (!patched.is_object()) {
        return tl::unexpected(problem_response(403,
                                               "Forbidden",
                                               "the patch replaces the whole NF profile",
                                               std::string("MODIFICATION_NOT_ALLOWED")));
    }
    for (const char* required : {"nfInstanceId", "nfType", "nfStatus"}) {
        if (!patched.contains(required)) {
            return tl::unexpected(
                problem_response(403,
                                 "Forbidden",
                                 std::string("the patch removes the mandatory '") + required + "'",
                                 std::string("MODIFICATION_NOT_ALLOWED")));
        }
    }
    if (!patched.at("nfInstanceId").is_string() ||
        patched.at("nfInstanceId").get<std::string>() != path_id) {
        return tl::unexpected(problem_response(403,
                                               "Forbidden",
                                               "nfInstanceId is the resource's identity",
                                               std::string("MODIFICATION_NOT_ALLOWED")));
    }
    if (auto err = validate(patched); err.has_value()) {
        return tl::unexpected(problem_response(
            400, "Invalid NFProfile", *err, std::string("UNSPECIFIED_MSG_FAILURE")));
    }
    return patched;
}

tl::expected<std::string, Response> subscription_validity_time(const nlohmann::json& ops) {
    auto reject = [](const std::string& detail) {
        return tl::unexpected(
            problem_response(400, "Bad Request", detail, std::string("INVALID_MSG_FORMAT")));
    };
    std::optional<std::string> value;
    for (const auto& op : ops) {
        if (!op.is_object() || op.value("op", "") != "replace" ||
            op.value("path", "") != "/validityTime") {
            return reject("UpdateSubscription only replaces /validityTime (TS 29.510 5.2.2.5.6)");
        }
        const auto it = op.find("value");
        if (it == op.end() || !it->is_string() ||
            !sbi_core::parse_rfc3339(it->get<std::string>()).has_value()) {
            return reject("validityTime must be an RFC 3339 date-time");
        }
        value = it->get<std::string>();
    }
    return *value; // ops is non-empty (parse_json_patch_body)
}

} // namespace nrf
