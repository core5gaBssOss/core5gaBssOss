#include "monitoring_mapping.hpp"

namespace nef {

namespace {

bool starts_with(const std::string& s, const std::string& p) {
    return s.rfind(p, 0) == 0;
}

std::optional<std::int64_t> loss_of_connect_reason(const std::string& reason) {
    if (reason == "DEREGISTERED") {
        return 6;
    }
    if (reason == "MAX_DETECTION_TIME_EXPIRED") {
        return 7;
    }
    if (reason == "PURGED") {
        return 8;
    }
    if (reason == "UNAVAILABLE_PERIOD") {
        return 9;
    }
    return std::nullopt;
}

} // namespace

AfMonitoringPlan af_monitoring_reports(const nlohmann::json& udm_reports,
                                       const sbi_gen::MonitoringEventSubscription& sub) {
    using nlohmann::json;
    AfMonitoringPlan plan;
    const json list = udm_reports.is_array() ? udm_reports : json::array({udm_reports});
    for (const auto& r : list) {
        if (!r.is_object() || !r.contains("eventType")) {
            continue;
        }
        const auto type = r.at("eventType").is_string() ? r.at("eventType").get<std::string>()
                                                        : r.at("eventType").dump();
        const json report = r.value("report", json::object());
        json out{{"monitoringType", sub.monitoringType}};
        if (r.contains("timeStamp")) {
            out["eventTime"] = r.at("timeStamp");
        }
        const auto gpsi = r.value("gpsi", std::string{});
        if (starts_with(gpsi, "extid-")) {
            out["externalId"] = gpsi.substr(6);
        } else if (starts_with(gpsi, "msisdn-")) {
            out["msisdn"] = gpsi.substr(7);
        } else if (sub.externalId.has_value()) {
            out["externalId"] = *sub.externalId;
        } else if (sub.msisdn.has_value()) {
            out["msisdn"] = *sub.msisdn;
        }

        if (type == "LOSS_OF_CONNECTIVITY") {
            if (const auto v = loss_of_connect_reason(report.value("lossOfConnectReason", ""));
                v.has_value()) {
                out["lossOfConnectReason"] = *v;
            }
        } else if (type == "UE_REACHABILITY_FOR_DATA" || type == "UE_REACHABILITY_FOR_SMS") {
            const bool sms = type == "UE_REACHABILITY_FOR_SMS";
            out["reachabilityType"] = sms ? "SMS" : "DATA";
            const auto key = sms ? "reachabilityForSmsReport" : "reachabilityReport";
            if (r.contains(key) && r.at(key).contains("maxAvailabilityTime")) {
                out["maxUEAvailabilityTime"] = r.at(key).at("maxAvailabilityTime");
            }
        } else if (type == "LOCATION_REPORTING") {
            if (report.contains("location")) {
                out["locationInfo"] = json{{"userLocation", report.at("location")}};
            }
        } else if (type == "ROAMING_STATUS") {
            if (report.contains("roaming")) {
                out["roamingStatus"] = report.at("roaming");
            }
            if (report.contains("newServingPlmn")) {
                out["plmnId"] = report.at("newServingPlmn");
            }
        } else if (type == "CHANGE_OF_SUPI_PEI_ASSOCIATION") {
            const auto pei = report.value("newPei", std::string{});
            if (!pei.empty()) {
                out["pei"] = pei;
                if (starts_with(pei, "imeisv-")) {
                    out["imeiChange"] = "IMEISV";
                } else if (starts_with(pei, "imei-")) {
                    out["imeiChange"] = "IMEI";
                }
            }
        } else if (type != "COMMUNICATION_FAILURE" && type != "AVAILABILITY_AFTER_DDN_FAILURE") {
            plan.not_mapped.push_back(type);
            continue;
        }
        plan.reports.push_back(out.get<sbi_gen::MonitoringEventReport>());
    }
    return plan;
}

} // namespace nef
