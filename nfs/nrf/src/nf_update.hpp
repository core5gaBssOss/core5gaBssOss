#pragma once

#include "sbi_core/http2_server.hpp"

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <tl/expected.hpp>

// Private to nfs/nrf. NFUpdate partial update / heart-beat (TS 29.510 5.2.2.3.1B, 5.2.2.3.2) and
// UpdateSubscription (5.2.2.5.6) request checks, kept free of the server so they test without
// ports (ADR-0574).

namespace nrf {

// A strong entity tag (RFC 9110 8.8.3) for a stored NF profile: a quoted hash of its serialised
// form. nlohmann::json keeps object members sorted, so equal profiles give equal tags. Stable
// within one NRF process only, which is as long as the in-memory profile lives.
std::string profile_etag(const nlohmann::json& profile);

// RFC 9110 13.1.1 If-Match with the strong comparison: "*" or a comma-separated list of entity
// tags, one of which equals `current_etag`. A weak tag (W/"...") never matches.
bool if_match_satisfied(std::string_view if_match, const std::string& current_etag);

// Returns the profile to store, or the response to send instead:
// - If-Match present and not satisfied: 412 (5.2.2.3.1B step 2b);
// - an op that RFC 6902 cannot apply (a missing path, a failed "test"): 409 (step 2b, "e.g. to
//   change value of a non-existing IE"); a structurally broken PatchItem: 400 INVALID_MSG_FORMAT;
// - a result without nfInstanceId/nfType/nfStatus, or whose nfInstanceId differs from the path:
//   403 MODIFICATION_NOT_ALLOWED (TS 29.500 table 5.2.7.2-1);
// - a result `validate` rejects: 400 UNSPECIFIED_MSG_FAILURE, as NFRegister answers the same
//   profile.
// `ops` has already passed sbi_core::http2::parse_json_patch_body.
tl::expected<nlohmann::json, sbi_core::http2::Response>
patch_nf_profile(const nlohmann::json& current,
                 const nlohmann::json& ops,
                 const std::string& path_id,
                 const std::optional<std::string>& if_match,
                 const std::function<std::optional<std::string>(const nlohmann::json&)>& validate);

// UpdateSubscription (5.2.2.5.6 step 1): the PatchItem array may only "replace" "/validityTime"
// with an RFC 3339 date-time. Returns that value, or 400 INVALID_MSG_FORMAT (step 2c).
tl::expected<std::string, sbi_core::http2::Response>
subscription_validity_time(const nlohmann::json& ops);

} // namespace nrf
