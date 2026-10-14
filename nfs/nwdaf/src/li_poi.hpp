#pragma once

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>

#include "li_core/xiri.hpp"
#include "li_poi/poi_runtime.hpp"

// The NWDAF's Lawful Interception IRI-POI (TS 33.127 clause 7.18, TS 33.128 clause 7.16,
// ADR-0489). Private to nfs/nwdaf; shares the generic X1/X2 runtime (libs/li-poi). Targets: SUPI
// only (SUPIIMSI, SUPINAI; table 7.16.1.1-1).
//
// Events:
// - Nnwdaf_EventsSubscription Subscribe (POST), update (PUT), Unsubscribe (DELETE) answered for a
//   target -> NWDAFEventsSubscription.
// - Nnwdaf_EventsSubscription_Notify sent for a subscription naming a target ->
//   NWDAFEventsNotification.
// - Nnwdaf_AnalyticsInfo_Request answered for a target -> NWDAFAnalyticsInfoQuery.
// Roaming analytics records are not generated: this NWDAF does not serve Nnwdaf_RoamingAnalytics.
//
// Target matching (7.16.2.1.1, "only when the target SUPI is the single SUPI for which the
// analytics is run"): an event subscription / query whose event is one of the seven UE-related
// analytics of TS 33.127 7.18.1 and whose tgtUe names exactly one SUPI, with anyUe not true.
//
// nWDAFConsumerNFType (mandatory) is the NF type the request's User-Agent starts with (TS 29.500
// 5.2.2.2). Absent or outside the ENUMERATED -> the subscription record is withheld and logged,
// never defaulted. Every NF here sends its NFType since ADR-0490; the ADRF is outside the
// ENUMERATED, so its subscriptions are withheld.

namespace nwdaf {

class NwdafLiPoi {
public:
    explicit NwdafLiPoi(li_poi::Config config);
    ~NwdafLiPoi();
    NwdafLiPoi(const NwdafLiPoi&) = delete;
    NwdafLiPoi& operator=(const NwdafLiPoi&) = delete;

    void start();
    void stop();

    enum class SubscriptionOp { Post, Put, Delete };

    // `subscription` is the NnwdafEventsSubscription as received (POST / PUT) or as stored
    // (DELETE); `user_agent` the request's User-Agent ("" if absent); `resource_uri` the
    // subscription's URI when the operation succeeded.
    void report_events_subscription(const std::string& subscription,
                                    SubscriptionOp op,
                                    const std::string& user_agent,
                                    const std::optional<std::string>& resource_uri,
                                    int http_status);

    // `subscription` is the stored NnwdafEventsSubscription the notification is for (it carries the
    // target), `notification` the NnwdafEventsSubscriptionNotification exactly as sent.
    void report_events_notification(const nlohmann::json& subscription,
                                    const std::string& notification);

    // The Nnwdaf_AnalyticsInfo_Request query parameters as received (absent = nullopt) and the
    // response the NWDAF gave.
    void report_analytics_info_query(const std::string& event_id,
                                     const std::optional<std::string>& tgt_ue,
                                     const std::optional<std::string>& event_filter,
                                     const std::optional<std::string>& ana_req,
                                     int http_status,
                                     const std::string& response_body);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nwdaf
