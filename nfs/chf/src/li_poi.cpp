#include "li_poi.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "li_core/xiri.hpp"
#include "li_poi/user_location.hpp"

namespace chf {

namespace xiri = li_core::xiri;
namespace x1 = li_core::x1;
using nlohmann::json;

namespace {

std::string bare_identity(const std::string& s) {
    for (const char* prefix : {"imsi-", "nai-", "supi-", "imei-", "imeisv-", "msisdn-", "extid-"}) {
        const std::size_t len = std::char_traits<char>::length(prefix);
        if (s.size() >= len && s.compare(0, len, prefix) == 0) {
            return s.substr(len);
        }
    }
    return s;
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

// TS 29.571 "imei-<15 digits>" carries the check digit; the X1 IMEI target (14 digits) does not.
std::string pei_for_matching(const std::string& pei) {
    std::string bare = bare_identity(pei);
    if (starts_with(pei, "imei-") && bare.size() == 15) {
        bare.pop_back();
    }
    return bare;
}

using K = x1::TargetIdentifierKind;
// TS 33.128 clause 7.19.1 (SIP-URI and IMEISV have no modelled X1 kind here; disclosed).
const std::vector<K> kSupportedKinds{K::SupiImsi,
                                     K::SupiNai,
                                     K::Imsi,
                                     K::GpsiMsisdn,
                                     K::GpsiNai,
                                     K::Msisdn,
                                     K::PeiImei,
                                     K::PeiImeisv,
                                     K::Imei};

// ChargingDataRequest service-specific members (TS32291_Nchf_ConvergedCharging.yaml) -> table
// 7.19.3.7-1. n2ConnectionChargingInformation(5) has no member in the R19 ChargingDataRequest.
const std::vector<std::pair<const char*, xiri::NchfChargingEvent>> kServiceInfo{
    {"pDUSessionChargingInformation", xiri::NchfChargingEvent::PduSessionChargingInformation},
    {"sMSChargingInformation", xiri::NchfChargingEvent::SmsChargingInformation},
    {"nEFChargingInformation", xiri::NchfChargingEvent::NefChargingInformation},
    {"registrationChargingInformation", xiri::NchfChargingEvent::RegistrationChargingInformation},
    {"locationReportingChargingInformation",
     xiri::NchfChargingEvent::LocationReportingChargingInformation},
    {"nSPAChargingInformation", xiri::NchfChargingEvent::NspaChargingInformation},
    {"nSMChargingInformation", xiri::NchfChargingEvent::NsmChargingInformation},
    {"mMTelChargingInformation", xiri::NchfChargingEvent::MmTelChargingInformation},
    {"iMSChargingInformation", xiri::NchfChargingEvent::ImsChargingInformation},
    {"edgeInfrastructureUsageChargingInformation",
     xiri::NchfChargingEvent::EdgeInfrastructureUsageChargingInformation},
    {"eASDeploymentChargingInformation", xiri::NchfChargingEvent::EasDeploymentChargingInformation},
    {"directEdgeEnablingServiceChargingInformation",
     xiri::NchfChargingEvent::DirectEdgeEnablingServiceChargingInformation},
    {"exposedEdgeEnablingServiceChargingInformation",
     xiri::NchfChargingEvent::ExposedEdgeEnablingServiceChargingInformation},
    {"proSeChargingInformation", xiri::NchfChargingEvent::ProSeChargingInformation},
    {"mMSChargingInformation", xiri::NchfChargingEvent::MmsChargingInformation},
    {"mBSSessionChargingInformation", xiri::NchfChargingEvent::MbsSessionChargingInformation},
    {"tSNChargingInformation", xiri::NchfChargingEvent::TsnChargingInformation},
    {"nSACFChargingInformation", xiri::NchfChargingEvent::NsacfChargingInformation},
    {"nSSAAChargingInformation", xiri::NchfChargingEvent::NssaaChargingInformation},
    {"rangingSLChargingInformation", xiri::NchfChargingEvent::RangingSlChargingInformation},
};

constexpr const char* kConvergedRef =
    "TS32291_Nchf_ConvergedCharging.yaml#/components/schemas/ChargingDataRequest";
constexpr const char* kOfflineRef =
    "TS32291_Nchf_OfflineOnlyCharging.yaml#/components/schemas/ChargingDataRequest";

std::optional<std::string> string_at(const json& obj, const char* key) {
    if (obj.is_object() && obj.contains(key) && obj.at(key).is_string()) {
        return obj.at(key).get<std::string>();
    }
    return std::nullopt;
}

} // namespace

struct ChfLiPoi::Impl {
    explicit Impl(li_poi::Config cfg) : runtime(std::move(cfg), make_hooks()) {}

    static li_poi::Hooks make_hooks() {
        li_poi::Hooks h;
        h.log_name = "chf-li-poi";
        h.supported_kinds = kSupportedKinds;
        h.normalise = [](const std::string& s) { return bare_identity(s); };
        return h;
    }

    std::vector<li_poi::Match> matches(const std::optional<std::string>& supi,
                                       const std::optional<std::string>& gpsi,
                                       const std::optional<std::string>& pei) const {
        std::vector<li_poi::Match> out;
        const auto add = [&](std::vector<li_poi::Match> found) {
            for (auto& m : found) {
                if (std::none_of(
                        out.begin(), out.end(), [&](const auto& o) { return o.xid == m.xid; })) {
                    out.push_back(std::move(m));
                }
            }
        };
        if (supi) {
            add(runtime.matches(*supi, {K::SupiImsi, K::SupiNai}));
            if (starts_with(*supi, "imsi-")) {
                add(runtime.matches(*supi, {K::Imsi}));
            }
        }
        if (gpsi) {
            add(runtime.matches(*gpsi, {K::GpsiMsisdn, K::GpsiNai}));
            if (starts_with(*gpsi, "msisdn-")) {
                add(runtime.matches(*gpsi, {K::Msisdn}));
            }
        }
        if (pei) {
            add(runtime.matches(pei_for_matching(*pei), {K::PeiImei, K::PeiImeisv, K::Imei}));
        }
        return out;
    }

    li_poi::PoiRuntime runtime;
};

ChfLiPoi::ChfLiPoi(li_poi::Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}
ChfLiPoi::~ChfLiPoi() = default;

void ChfLiPoi::start() {
    impl_->runtime.start();
}
void ChfLiPoi::stop() {
    impl_->runtime.stop();
}

void ChfLiPoi::report_charging_data_request(const std::string& body, bool offline) {
    const json req = json::parse(body, nullptr, false);
    if (!req.is_object()) {
        return; // not a ChargingDataRequest the CHF could have handled; nothing to attribute
    }
    // The service-specific charging information present (table 7.19.3.4-1 NOTE: the additional
    // attributes of TS 32.291 clause 6.1.6.2); the first one in table 7.19.3.7-1 order.
    std::optional<xiri::NchfChargingEvent> event;
    const json* service = nullptr;
    for (const auto& [member, value] : kServiceInfo) {
        if (req.contains(member) && req.at(member).is_object()) {
            event = value;
            service = &req.at(member);
            break;
        }
    }
    const json empty = json::object();
    const json& user_info = service != nullptr && service->contains("userInformation")
                                ? service->at("userInformation")
                                : empty;

    const auto found = impl_->matches(string_at(req, "subscriberIdentifier"),
                                      string_at(user_info, "servedGPSI"),
                                      string_at(user_info, "servedPEI"));
    if (found.empty()) {
        return;
    }

    // ADR-0488 (decision B): roamerInOut is mandatory in the ASN.1 and only the request can say
    // which way a subscriber roams -- never defaulted.
    const auto roamer = string_at(user_info, "roamerInOut");
    std::optional<xiri::RoamerInOut> roamer_in_out;
    if (roamer == "IN_BOUND") {
        roamer_in_out = xiri::RoamerInOut::InBound;
    } else if (roamer == "OUT_BOUND") {
        roamer_in_out = xiri::RoamerInOut::OutBound;
    }

    xiri::ChargingDataEvent rec;
    rec.offline = offline;
    rec.charging_data = xiri::SbiType{offline ? kOfflineRef : kConvergedRef, body};
    rec.charging_event = event;
    if (service != nullptr && service->contains("userLocationinfo")) {
        rec.location = li_poi::location_from_user_location(service->at("userLocationinfo"));
    }
    std::optional<std::vector<std::uint8_t>> payload;

    for (const auto& m : found) {
        const auto task = impl_->runtime.task(m.xid);
        if (!task || !task->charging_data_events) {
            continue; // table 7.19.1-1: no ChargingDataEvents extension -> no records
        }
        if (*task->charging_data_events ==
            x1::ChargingDataEventsGenerated::TranslatedChargingDataInformation) {
            spdlog::error("chf-li-poi: task {} asks for TranslatedChargingDataInformation, which "
                          "this POI does not build -- ChargingDataEvent not sent (ADR-0488)",
                          m.xid);
            continue;
        }
        if (!roamer_in_out) {
            spdlog::warn("chf-li-poi: ChargingDataRequest for task {} carries no "
                         "UserInformation.roamerInOut, mandatory in the TS 33.128 j70 ASN.1 -- "
                         "ChargingDataEvent withheld (ADR-0488, strict ASN.1)",
                         m.xid);
            continue;
        }
        rec.roamer_in_out = *roamer_in_out;
        if (!payload) {
            auto encoded = xiri::encode_xiri_payload(rec);
            if (!encoded) {
                spdlog::error("chf-li-poi: could not encode ChargingDataEvent: {}",
                              encoded.error());
                return;
            }
            payload = std::move(*encoded);
        }
        impl_->runtime.emit(
            m, *payload, li_core::PayloadDirection::NotApplicable, "ChargingDataEvent");
    }
}

} // namespace chf
