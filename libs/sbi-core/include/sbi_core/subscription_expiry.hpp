#pragma once

#include "sbi_core/datetime.hpp"
#include "sbi_core/store_quota.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>

namespace sbi_core {

// REVIEW-5ae0ef1 SUS-02 (ADR-0535, shared since ADR-0564): a subscribe request proposes a
// subscription's expiry and the response carries the one the producer confirms -- TS 29.503
// 6.1.6.2.3 / 6.4.6.2.6 for the UDM, TS 29.504 5.2.2.6.2 / 5.3.2.4.2 and TS 29.519 for the UDR.
// Confirms the value at `at` in `doc` (sbi_core::confirm_expiry: at most `max_lifetime` ahead,
// spread when the producer sets it), writing it back when it differs from the proposal; a proposal
// accepted as is keeps the consumer's own spelling. Absent or null: the producer sets one.
// Throws std::invalid_argument when the value is not an RFC 3339 date-time.
inline std::chrono::system_clock::time_point
confirm_expiry_in(nlohmann::json& doc,
                  const nlohmann::json::json_pointer& at,
                  std::chrono::seconds max_lifetime,
                  std::chrono::system_clock::time_point now = std::chrono::system_clock::now()) {
    std::optional<std::chrono::system_clock::time_point> proposed;
    if (doc.contains(at) && !doc.at(at).is_null()) {
        if (doc.at(at).is_string()) {
            proposed = parse_rfc3339(doc.at(at).get<std::string>());
        }
        if (!proposed) {
            throw std::invalid_argument(at.to_string() + " is not an RFC 3339 date-time");
        }
    }
    const auto confirmed = confirm_expiry(proposed, now, max_lifetime);
    if (confirmed != proposed) {
        doc[at] = format_rfc3339(confirmed);
    }
    return confirmed;
}

} // namespace sbi_core
