#include "li_poi.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace nwdaf {

namespace xiri = li_core::xiri;
namespace x1 = li_core::x1;
using json = nlohmann::json;

namespace {

// Same SBI-identity-scheme stripper every POI in this project carries.
std::string bare_identity(const std::string& s) {
    for (const char* prefix : {"imsi-", "nai-", "supi-"}) {
        const std::size_t len = std::char_traits<char>::length(prefix);
        if (s.size() >= len && s.compare(0, len, prefix) == 0) {
            return s.substr(len);
        }
    }
    return s;
}

using K = x1::TargetIdentifierKind;
// TS 33.128 table 7.16.1.1-1.
const std::vector<K> kSupiKinds{K::SupiImsi, K::SupiNai};

// The SBIReference strings exactly as tables 7.16.2.2-1 / 7.16.2.3-1 / 7.16.2.4-1 give them.
constexpr const char* kSubscriptionRef =
    "TS29520_Nnwdaf_EventsSubscription.yaml#/components/schemas/NnwdafEventsSubscription";
constexpr const char* kNotificationRef =
    "TS29520_Nnwdaf_EventsSubscription.yaml#/components/schemas/"
    "NnwdafEventsSubscriptionNotification";
constexpr const char* kReportingRequirementRef =
    "TS29520_Nnwdaf_AnalyticsInfo.yaml#/components/schemas/EventReportingRequirement";
constexpr const char* kEventFilterRef =
    "TS29520_Nnwdaf_AnalyticsInfo.yaml#/components/schemas/EventFilter";
constexpr const char* kAnalyticsDataRef =
    "TS29520_Nnwdaf_AnalyticsInfo.yaml#/components/schemas/AnalyticsData";

// TS 29.520 NwdafEvent -> NWDAFEvent: the seven UE-related analytics of TS 33.127 7.18.1. Any other
// analytic is not of LI interest (7.18.1: "LI for these services is not defined").
std::optional<xiri::NwdafEvent> li_event(const std::string& e) {
    using E = xiri::NwdafEvent;
    if (e == "SERVICE_EXPERIENCE") {
        return E::ServiceExperience;
    }
    if (e == "UE_MOBILITY") {
        return E::UeMobility;
    }
    if (e == "UE_COMMUNICATION") {
        return E::UeComm;
    }
    if (e == "ABNORMAL_BEHAVIOUR") {
        return E::AbnormalBehaviour;
    }
    if (e == "DISPERSION") {
        return E::Dispersion;
    }
    if (e == "RELATIVE_PROXIMITY") {
        return E::RelativeProximity;
    }
    if (e == "PDU_SESSION_TRAFFIC") {
        return E::PduSessionTraffic;
    }
    return std::nullopt;
}

// 7.16.2.1.1: the analytics is run for a single SUPI -- tgtUe.supis has exactly one entry and anyUe
// is not true.
std::optional<std::string> single_supi(const json& tgt) {
    if (!tgt.is_object()) {
        return std::nullopt;
    }
    if (tgt.contains("anyUe") && tgt.at("anyUe").is_boolean() && tgt.at("anyUe").get<bool>()) {
        return std::nullopt;
    }
    if (!tgt.contains("supis") || !tgt.at("supis").is_array() || tgt.at("supis").size() != 1 ||
        !tgt.at("supis").at(0).is_string()) {
        return std::nullopt;
    }
    return tgt.at("supis").at(0).get<std::string>();
}

// TS 29.500 5.2.2.2: the User-Agent starts with the TS 29.510 NFType, then "-" and anything else.
std::optional<xiri::NwdafConsumerNfType> consumer_nf_type(const std::string& user_agent) {
    std::string nf = user_agent.substr(0, user_agent.find('-'));
    std::ranges::transform(
        nf, nf.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    using T = xiri::NwdafConsumerNfType;
    // OAM (8) is not an NFType, so no User-Agent can carry it.
    static const std::vector<std::pair<std::string, T>> kTypes{{"PCF", T::Pcf},
                                                               {"NSSF", T::Nssf},
                                                               {"AMF", T::Amf},
                                                               {"SMF", T::Smf},
                                                               {"NEF", T::Nef},
                                                               {"AF", T::Af},
                                                               {"LMF", T::Lmf},
                                                               {"NWDAF", T::Nwdaf},
                                                               {"DCCF", T::Dccf},
                                                               {"CEF", T::Cef}};
    for (const auto& [name, type] : kTypes) {
        if (nf == name) {
            return type;
        }
    }
    return std::nullopt;
}

xiri::Supi to_supi(const std::string& supi) {
    const std::string bare = bare_identity(supi);
    return supi.rfind("nai-", 0) == 0 ? xiri::Supi{xiri::Nai{bare}} : xiri::Supi{xiri::Imsi{bare}};
}

// Per single-SUPI target in a NnwdafEventsSubscription, the UE-related events subscribed for it,
// in request order, each once.
std::vector<std::pair<std::string, std::vector<xiri::NwdafEvent>>>
targets_of(const json& subscription) {
    std::vector<std::pair<std::string, std::vector<xiri::NwdafEvent>>> out;
    if (!subscription.is_object() || !subscription.contains("eventSubscriptions") ||
        !subscription.at("eventSubscriptions").is_array()) {
        return out;
    }
    for (const auto& es : subscription.at("eventSubscriptions")) {
        if (!es.is_object() || !es.contains("event") || !es.at("event").is_string() ||
            !es.contains("tgtUe")) {
            continue;
        }
        const auto event = li_event(es.at("event").get<std::string>());
        const auto supi = single_supi(es.at("tgtUe"));
        if (!event || !supi) {
            continue;
        }
        auto it = std::ranges::find_if(out, [&](const auto& p) { return p.first == *supi; });
        if (it == out.end()) {
            out.emplace_back(*supi, std::vector<xiri::NwdafEvent>{});
            it = std::prev(out.end());
        }
        if (std::ranges::find(it->second, *event) == it->second.end()) {
            it->second.push_back(*event);
        }
    }
    return out;
}

std::optional<json> parse_object(const std::optional<std::string>& text) {
    if (!text) {
        return std::nullopt;
    }
    json j = json::parse(*text, nullptr, false);
    if (j.is_discarded()) {
        return std::nullopt;
    }
    return j;
}

} // namespace

struct NwdafLiPoi::Impl {
    explicit Impl(li_poi::Config cfg) : runtime(std::move(cfg), make_hooks()) {}

    static li_poi::Hooks make_hooks() {
        li_poi::Hooks h;
        h.log_name = "nwdaf-li-poi";
        h.supported_kinds = kSupiKinds;
        h.normalise = [](const std::string& s) { return bare_identity(s); };
        return h;
    }

    void emit(const std::vector<li_poi::Match>& found, const xiri::Event& rec, const char* name) {
        const auto payload = xiri::encode_xiri_payload(rec);
        if (!payload) {
            spdlog::error("nwdaf-li-poi: could not encode {}: {}", name, payload.error());
            return;
        }
        for (const auto& m : found) {
            runtime.emit(m, *payload, li_core::PayloadDirection::NotApplicable, name);
        }
    }

    li_poi::PoiRuntime runtime;
};

NwdafLiPoi::NwdafLiPoi(li_poi::Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}
NwdafLiPoi::~NwdafLiPoi() = default;

void NwdafLiPoi::start() {
    impl_->runtime.start();
}
void NwdafLiPoi::stop() {
    impl_->runtime.stop();
}

void NwdafLiPoi::report_events_subscription(const std::string& subscription,
                                            SubscriptionOp op,
                                            const std::string& user_agent,
                                            const std::optional<std::string>& resource_uri,
                                            int http_status) {
    const json body = json::parse(subscription, nullptr, false);
    if (body.is_discarded()) {
        return;
    }
    for (const auto& [supi, events] : targets_of(body)) {
        const auto found = impl_->runtime.matches(supi, kSupiKinds);
        if (found.empty()) {
            continue;
        }
        const auto consumer = consumer_nf_type(user_agent);
        if (!consumer) {
            // nWDAFConsumerNFType is mandatory; nothing in the request identifies it (ADR-0489).
            spdlog::error("nwdaf-li-poi: NWDAFEventsSubscription for a target withheld -- consumer "
                          "NF type unknown (User-Agent '{}')",
                          user_agent);
            continue;
        }
        const auto code = xiri::to_nwdaf_events_subscription_response_code(http_status);
        if (!code) {
            spdlog::error("nwdaf-li-poi: HTTP {} has no NWDAFEventsSubscriptionResponseCode value "
                          "-- NWDAFEventsSubscription not sent",
                          http_status);
            continue;
        }
        xiri::NwdafEventsSubscription rec;
        rec.supi = to_supi(supi);
        rec.consumer_nf_type = *consumer;
        rec.op_type = op == SubscriptionOp::Post  ? xiri::NwdafEventsSubscriptionOpType::Post
                      : op == SubscriptionOp::Put ? xiri::NwdafEventsSubscriptionOpType::Put
                                                  : xiri::NwdafEventsSubscriptionOpType::Delete;
        rec.subscribed_events = events;
        rec.subscription = xiri::SbiType{kSubscriptionRef, subscription};
        rec.subscription_id = resource_uri;
        rec.response_code = *code;
        impl_->emit(found, rec, "NWDAFEventsSubscription");
    }
}

void NwdafLiPoi::report_events_notification(const json& subscription,
                                            const std::string& notification) {
    const json note = json::parse(notification, nullptr, false);
    if (note.is_discarded() || !note.contains("eventNotifications") ||
        !note.at("eventNotifications").is_array()) {
        return;
    }
    std::vector<xiri::NwdafEvent> sent;
    for (const auto& en : note.at("eventNotifications")) {
        if (en.is_object() && en.contains("event") && en.at("event").is_string()) {
            if (const auto e = li_event(en.at("event").get<std::string>())) {
                sent.push_back(*e);
            }
        }
    }
    for (const auto& [supi, events] : targets_of(subscription)) {
        std::vector<xiri::NwdafEvent> notified;
        for (const auto e : events) {
            if (std::ranges::find(sent, e) != sent.end()) {
                notified.push_back(e);
            }
        }
        if (notified.empty()) {
            continue;
        }
        const auto found = impl_->runtime.matches(supi, kSupiKinds);
        if (found.empty()) {
            continue;
        }
        xiri::NwdafEventsNotification rec;
        rec.supi = to_supi(supi);
        rec.notified_events = std::move(notified);
        rec.notification = xiri::SbiType{kNotificationRef, notification};
        impl_->emit(found, rec, "NWDAFEventsNotification");
    }
}

void NwdafLiPoi::report_analytics_info_query(const std::string& event_id,
                                             const std::optional<std::string>& tgt_ue,
                                             const std::optional<std::string>& event_filter,
                                             const std::optional<std::string>& ana_req,
                                             int http_status,
                                             const std::string& response_body) {
    const auto event = li_event(event_id);
    const auto target = parse_object(tgt_ue);
    if (!event || !target) {
        return;
    }
    const auto supi = single_supi(*target);
    if (!supi) {
        return;
    }
    const auto found = impl_->runtime.matches(*supi, kSupiKinds);
    if (found.empty()) {
        return;
    }
    const auto code = xiri::to_nwdaf_analytics_info_response_code(http_status);
    if (!code) {
        spdlog::error("nwdaf-li-poi: HTTP {} has no NWDAFAnalyticsInfoResponseCode value -- "
                      "NWDAFAnalyticsInfoQuery not sent",
                      http_status);
        return;
    }
    xiri::NwdafAnalyticsInfoQuery rec;
    rec.supi = to_supi(*supi);
    rec.event = *event;
    if (ana_req) {
        rec.reporting_requirement = xiri::SbiType{kReportingRequirementRef, *ana_req};
    }
    if (event_filter) {
        rec.event_filter = xiri::SbiType{kEventFilterRef, *event_filter};
    }
    // Table 7.16.2.4-1: the AnalyticsData "present in the response when the response code is 200".
    if (http_status == 200 && !response_body.empty()) {
        rec.analytics_data = xiri::SbiType{kAnalyticsDataRef, response_body};
    }
    rec.response_code = *code;
    impl_->emit(found, rec, "NWDAFAnalyticsInfoQuery");
}

} // namespace nwdaf
