// ADR-0538 (REVIEW-5ae0ef1 SUS-02): the five expiries the NEF's APIs make mandatory, as the sweep
// decides them (nfs/nef/src/expiry.cpp).

#include "sbi_core/datetime.hpp"

#include <nlohmann/json.hpp>

#include <chrono>

#include "expiry.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;
using namespace std::chrono_literals;

const auto kNow = nef::Clock::now();

std::string at(std::chrono::seconds offset) {
    return sbi_core::format_rfc3339(kNow + offset);
}

} // namespace

TEST(NefExpiry, AMonitoringSubscriptionGoesAtItsMonitorExpireTime) {
    EXPECT_EQ(nef::expire_monitoring(json{{"monitorExpireTime", at(-1s)}}, kNow),
              nef::SweepAction::remove);
    EXPECT_EQ(nef::expire_monitoring(json{{"monitorExpireTime", at(60s)}}, kNow),
              nef::SweepAction::keep);
    EXPECT_EQ(nef::expire_monitoring(json{{"monitoringType", "LOCATION_REPORTING"}}, kNow),
              nef::SweepAction::keep)
        << "no monitorExpireTime, no expiry";
}

TEST(NefExpiry, AnEventExposureSubscriptionGoesAtItsCommonMonDur) {
    EXPECT_EQ(nef::expire_event_exposure(json{{"eventsRepInfo", {{"monDur", at(-1s)}}}}, kNow),
              nef::SweepAction::remove);
    EXPECT_EQ(nef::expire_event_exposure(json{{"eventsRepInfo", {{"monDur", at(60s)}}}}, kNow),
              nef::SweepAction::keep);
    // A per-event monDur (PerEventRepReq) is not the subscription's lifetime.
    EXPECT_EQ(
        nef::expire_event_exposure(json{{"eventsSubs",
                                         json::array({{{"event", "UE_MOBILITY"},
                                                       {"eventRepInfo", {{"monDur", at(-1s)}}}}})}},
                                   kNow),
        nef::SweepAction::keep);
}

TEST(NefExpiry, ATimeSyncSubscriptionGoesAtItsExpiryAndAConfigurationStays) {
    EXPECT_EQ(nef::expire_time_sync(json{{"expiry", at(-1s)}}, kNow), nef::SweepAction::remove);
    EXPECT_EQ(nef::expire_time_sync(json{{"expiry", at(60s)}}, kNow), nef::SweepAction::keep);
    EXPECT_EQ(
        nef::expire_time_sync(json{{"reqPtpIns", {{"instanceType", "BOUNDARY_CLOCK"}}}}, kNow),
        nef::SweepAction::keep);
}

TEST(NefExpiry, ANetworkParameterConfigurationGoesAtItsValidityTime) {
    EXPECT_EQ(nef::expire_np_configuration(json{{"validityTime", at(-1s)}}, kNow),
              nef::SweepAction::remove);
    EXPECT_EQ(nef::expire_np_configuration(json{{"validityTime", at(60s)}}, kNow),
              nef::SweepAction::keep);
    EXPECT_EQ(nef::expire_np_configuration(json{{"externalId", "x@example.com"}}, kNow),
              nef::SweepAction::keep);
}

TEST(NefExpiry, A5gLanProvisioningLosesOnlyItsExpiredNpConfigParams) {
    json doc{{"5gLanParams",
              {{"exterGroupId", "g@example.com"},
               {"npConfigParams", {{"maximumLatency", 5}, {"validityTime", at(-1s)}}}}}};
    EXPECT_EQ(nef::expire_5glan_np_config(doc, kNow), nef::SweepAction::rewrite);
    EXPECT_FALSE(doc.at("5gLanParams").contains("npConfigParams"));
    EXPECT_EQ(doc.at("5gLanParams").at("exterGroupId"), "g@example.com");
    EXPECT_EQ(nef::expire_5glan_np_config(doc, kNow), nef::SweepAction::keep);
}

TEST(NefExpiry, ACpProvisioningLosesItsExpiredSetsAndGoesWithTheLast) {
    json doc{{"cpParameterSets",
              json::array({{{"setId", "old"}, {"validityTime", at(-1s)}},
                           {{"setId", "new"}, {"validityTime", at(60s)}},
                           {{"setId", "forever"}}})}};
    EXPECT_EQ(nef::expire_cp_parameter_sets(doc, kNow), nef::SweepAction::rewrite);
    ASSERT_EQ(doc.at("cpParameterSets").size(), 2U);
    EXPECT_EQ(doc.at("cpParameterSets")[0].at("setId"), "new");
    EXPECT_EQ(nef::expire_cp_parameter_sets(doc, kNow), nef::SweepAction::keep);

    json last{{"cpParameterSets", json::array({{{"setId", "old"}, {"validityTime", at(-1s)}}})}};
    EXPECT_EQ(nef::expire_cp_parameter_sets(last, kNow), nef::SweepAction::remove);

    // An "Individual CP Set Provisioning" stored on its own.
    json set{{"setId", "alone"}, {"validityTime", at(-1s)}};
    EXPECT_EQ(nef::expire_cp_parameter_sets(set, kNow), nef::SweepAction::remove);
}

TEST(NefExpiry, AProposedExpiryIsCappedAndAnAbsentOneIsNotAdded) {
    json far{{"eventsRepInfo", {{"monDur", "2099-01-01T00:00:00Z"}}}};
    nef::confirm_proposed_expiry(far, json::json_pointer("/eventsRepInfo/monDur"), kNow, 3600s);
    const auto confirmed =
        sbi_core::parse_rfc3339(far.at("eventsRepInfo").at("monDur").get<std::string>());
    ASSERT_TRUE(confirmed.has_value());
    EXPECT_LE(*confirmed, kNow + 3600s);
    EXPECT_GE(*confirmed, kNow + 3600s - 361s) << "at most a tenth of the lifetime earlier";

    json near{{"expiry", at(60s)}};
    nef::confirm_proposed_expiry(near, json::json_pointer("/expiry"), kNow, 3600s);
    EXPECT_EQ(near.at("expiry"), at(60s)) << "a proposal inside the bound is kept";

    json none{{"notifId", "n"}};
    nef::confirm_proposed_expiry(none, json::json_pointer("/expiry"), kNow, 3600s);
    EXPECT_FALSE(none.contains("expiry"));
}
