#pragma once

#include <nlohmann/json.hpp>

#include <optional>

#include "li_core/xiri.hpp"

namespace li_poi {

// A TS 29.571 UserLocation JSON object (nrLocation / eutraLocation) -> the xIRI Location's
// locationInfo.userLocation, the encoding TS 33.128 asks every POI to use for a UE location it
// received over SBI. nullopt when neither member decodes -- a malformed location is omitted (it is
// a C field everywhere), never guessed. Shared by the SMSF and CHF POIs (ADR-0486, ADR-0488).
std::optional<li_core::xiri::Location> location_from_user_location(const nlohmann::json& ul);

} // namespace li_poi
