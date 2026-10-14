#include "li_poi/user_location.hpp"

#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace li_poi {

namespace xiri = li_core::xiri;
using nlohmann::json;

namespace {

std::optional<xiri::Plmnid> plmn_from(const json& j) {
    if (!j.is_object() || !j.contains("mcc") || !j.contains("mnc")) {
        return std::nullopt;
    }
    return xiri::Plmnid{j.at("mcc").get<std::string>(), j.at("mnc").get<std::string>()};
}

std::optional<std::vector<std::uint8_t>> hex_octets(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        try {
            out.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return out;
}

// TS 29.571 Tai: plmnId + tac (4 or 6 hex digits).
std::optional<xiri::Tai> tai_from(const json& j) {
    if (!j.is_object() || !j.contains("plmnId") || !j.contains("tac")) {
        return std::nullopt;
    }
    auto plmn = plmn_from(j.at("plmnId"));
    auto tac = hex_octets(j.at("tac").get<std::string>());
    if (!plmn || !tac || (tac->size() != 2 && tac->size() != 3)) {
        return std::nullopt;
    }
    return xiri::Tai{*plmn, *tac};
}

} // namespace

std::optional<xiri::Location> location_from_user_location(const json& ul) {
    try {
        xiri::UserLocation out;
        if (ul.contains("nrLocation")) {
            const auto& nr = ul.at("nrLocation");
            auto tai = tai_from(nr.at("tai"));
            auto plmn = plmn_from(nr.at("ncgi").at("plmnId"));
            const auto cell = nr.at("ncgi").at("nrCellId").get<std::string>(); // 9 hex digits
            if (tai && plmn && cell.size() == 9) {
                out.nr = xiri::NrLocation{*tai, xiri::Ncgi{*plmn, std::stoull(cell, nullptr, 16)}};
            }
        }
        if (ul.contains("eutraLocation")) {
            const auto& eu = ul.at("eutraLocation");
            auto tai = tai_from(eu.at("tai"));
            auto plmn = plmn_from(eu.at("ecgi").at("plmnId"));
            const auto cell = eu.at("ecgi").at("eutraCellId").get<std::string>(); // 7 hex digits
            if (tai && plmn && cell.size() == 7) {
                out.eutra = xiri::EutraLocation{
                    *tai,
                    xiri::Ecgi{*plmn, static_cast<std::uint32_t>(std::stoul(cell, nullptr, 16))}};
            }
        }
        if (!out.nr && !out.eutra) {
            return std::nullopt;
        }
        return xiri::Location{out};
    } catch (const std::exception&) {
        return std::nullopt; // malformed location: omitted (C field), never guessed
    }
}

} // namespace li_poi
