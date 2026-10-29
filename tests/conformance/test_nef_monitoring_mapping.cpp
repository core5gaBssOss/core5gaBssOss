// ADR-0527 (REVIEW-5ae0ef1 SPEC-06): the NEF's Nudm_EE MonitoringReport -> TS 29.122
// MonitoringEventReport mapping (TS 29.522 4.4.2). The notification is written as a sample for
// validate_structural_conformance.py (MonitoringNotification in TS29122_MonitoringEvent).

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "monitoring_mapping.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

sbi_gen::MonitoringEventSubscription subscription(const char* type) {
    return json{{"notificationDestination", "https://af.example.com/mon"},
                {"monitoringType", type},
                {"externalId", "ue1@af.example.com"}}
        .get<sbi_gen::MonitoringEventSubscription>();
}

TEST(NefMonitoringMapping, EachNudmEeReportBecomesTheAfsReport) {
    const json udm = json::array(
        {json{{"referenceId", 1},
              {"eventType", "LOSS_OF_CONNECTIVITY"},
              {"timeStamp", "2026-10-24T10:00:00Z"},
              {"gpsi", "msisdn-447700900123"},
              {"report", {{"lossOfConnectReason", "PURGED"}}}},
         json{{"referenceId", 1},
              {"eventType", "ROAMING_STATUS"},
              {"timeStamp", "2026-10-24T10:00:01Z"},
              {"report", {{"roaming", true}, {"newServingPlmn", {{"mcc", "001"}, {"mnc", "01"}}}}}},
         json{{"referenceId", 1},
              {"eventType", "CHANGE_OF_SUPI_PEI_ASSOCIATION"},
              {"timeStamp", "2026-10-24T10:00:02Z"},
              {"report", {{"newPei", "imeisv-4370816125816151"}}}},
         json{{"referenceId", 1},
              {"eventType", "UE_REACHABILITY_FOR_DATA"},
              {"timeStamp", "2026-10-24T10:00:03Z"},
              {"reachabilityReport", {{"maxAvailabilityTime", "2026-10-24T11:00:00Z"}}}},
         json{{"referenceId", 1},
              {"eventType", "LOCATION_REPORTING"},
              {"timeStamp", "2026-10-24T10:00:04Z"},
              {"report",
               {{"location",
                 {{"nrLocation",
                   {{"tai", {{"plmnId", {{"mcc", "001"}, {"mnc", "01"}}}, {"tac", "000001"}}},
                    {"ncgi",
                     {{"plmnId", {{"mcc", "001"}, {"mnc", "01"}}},
                      {"nrCellId", "000000001"}}}}}}}}}},
         json{{"referenceId", 1},
              {"eventType", "CM_CONNECTIVITY_STATE_CHANGE"},
              {"timeStamp", "2026-10-24T10:00:05Z"}}});
    const auto plan = nef::af_monitoring_reports(udm, subscription("LOSS_OF_CONNECTIVITY"));
    ASSERT_EQ(plan.reports.size(), 5U);
    EXPECT_EQ(plan.not_mapped, (std::vector<std::string>{"CM_CONNECTIVITY_STATE_CHANGE"}));

    const json r0 = plan.reports[0];
    EXPECT_EQ(r0.at("monitoringType"), "LOSS_OF_CONNECTIVITY");
    EXPECT_EQ(r0.at("lossOfConnectReason"), 8) << "TS 29.522 4.4.2: PURGED -> 8";
    EXPECT_EQ(r0.at("msisdn"), "447700900123") << "the UE from the report's GPSI";
    EXPECT_EQ(r0.at("eventTime"), "2026-10-24T10:00:00Z");
    const json r1 = plan.reports[1];
    EXPECT_EQ(r1.at("roamingStatus"), true);
    EXPECT_EQ(r1.at("plmnId").at("mcc"), "001");
    EXPECT_EQ(r1.at("externalId"), "ue1@af.example.com") << "no GPSI: the subscription's UE";
    const json r2 = plan.reports[2];
    EXPECT_EQ(r2.at("pei"), "imeisv-4370816125816151");
    EXPECT_EQ(r2.at("imeiChange"), "IMEISV");
    const json r3 = plan.reports[3];
    EXPECT_EQ(r3.at("reachabilityType"), "DATA");
    EXPECT_EQ(r3.at("maxUEAvailabilityTime"), "2026-10-24T11:00:00Z");
    const json r4 = plan.reports[4];
    EXPECT_TRUE(r4.at("locationInfo").at("userLocation").contains("nrLocation"));

    sbi_gen::MonitoringNotification n{};
    n.subscription = "https://nef/3gpp-monitoring-event/v1/af1/subscriptions/1";
    n.monitoringEventReports = plan.reports;
    const char* dir = std::getenv("SBI_CONFORMANCE_SAMPLES_DIR");
    const std::filesystem::path d = dir != nullptr ? dir : ".";
    std::filesystem::create_directories(d);
    std::ofstream(d / "nef_af_monitoring_notification.json") << json(n).dump(2);
}

TEST(NefMonitoringMapping, AnUnknownLossReasonIsOmittedNotInvented) {
    const json udm{{"referenceId", 1},
                   {"eventType", "LOSS_OF_CONNECTIVITY"},
                   {"timeStamp", "2026-10-24T10:00:00Z"},
                   {"report", {{"lossOfConnectReason", "SOMETHING_NEW"}}}};
    const auto plan = nef::af_monitoring_reports(udm, subscription("LOSS_OF_CONNECTIVITY"));
    ASSERT_EQ(plan.reports.size(), 1U);
    EXPECT_FALSE(json(plan.reports[0]).contains("lossOfConnectReason"));
}

} // namespace
