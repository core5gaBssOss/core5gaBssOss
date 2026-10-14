#include "li_poi.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

#include "diameter_core/dictionary.hpp"
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

// TS 32.299 / RFC 6733 / RFC 4006 AVP codes (TS_32.299_j00.txt clause 7.2).
constexpr std::uint32_t kUserName = 1;             // RFC 6733
constexpr std::uint32_t kServiceInformation = 873; // TS 32.299 7.2.192
constexpr std::uint32_t k3gppVendorId = 10415;
const std::vector<std::pair<std::uint32_t, xiri::RfChargingEvent>> kRfServiceInfo{
    {2054, xiri::RfChargingEvent::AocInformation},
    {874, xiri::RfChargingEvent::PsInformation},  // 7.2.158
    {876, xiri::RfChargingEvent::ImsInformation}, // 7.2.77
    {877, xiri::RfChargingEvent::MmsInformation},
    {878, xiri::RfChargingEvent::LcsInformation},
    {879, xiri::RfChargingEvent::PocInformation},
    {880, xiri::RfChargingEvent::MbmsInformation},
    {2000, xiri::RfChargingEvent::SmsInformation}, // 7.2.211
    {3410, xiri::RfChargingEvent::VcsInformation},
    {2030, xiri::RfChargingEvent::MmTelInformation},
    {3447, xiri::RfChargingEvent::ProSeInformation},
    {1256, xiri::RfChargingEvent::ServiceGenericInformation},
    {2110, xiri::RfChargingEvent::ImInformation},
    {2115, xiri::RfChargingEvent::DcdInformation},
    {3927, xiri::RfChargingEvent::CpdtInformation},
};

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

    // RFC 4006 Subscription-Id { Subscription-Id-Type, Subscription-Id-Data }: END_USER_E164 ->
    // MSISDN / GPSI-MSISDN targets, END_USER_IMSI -> IMSI / SUPI-IMSI, END_USER_NAI -> SUPI-NAI /
    // GPSI-NAI. END_USER_SIP_URI: no SIP-URI kind modelled (disclosed).
    std::vector<li_poi::Match> match_subscription_id(const diameter_core::Avp& sub) const {
        namespace d = diameter_core;
        const auto fields = d::decode_avps(sub.data);
        if (!fields) {
            return {};
        }
        const auto* type = d::find_avp(*fields, d::dictionary::Dcc::kSubscriptionIdType);
        const auto* data = d::find_avp(*fields, d::dictionary::Dcc::kSubscriptionIdData);
        if (type == nullptr || data == nullptr || type->data.size() != 4) {
            return {};
        }
        const auto t = static_cast<std::int32_t>(
            (std::uint32_t{type->data[0]} << 24) | (std::uint32_t{type->data[1]} << 16) |
            (std::uint32_t{type->data[2]} << 8) | std::uint32_t{type->data[3]});
        const std::string value(data->data.begin(), data->data.end());
        namespace st = d::dictionary::Dcc::SubscriptionIdType;
        switch (t) {
            case st::kEndUserE164:
                return runtime.matches(value, {K::Msisdn, K::GpsiMsisdn});
            case st::kEndUserImsi:
                return runtime.matches(value, {K::Imsi, K::SupiImsi});
            case st::kEndUserNai:
                return runtime.matches(value, {K::SupiNai, K::GpsiNai});
            default:
                return {};
        }
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
    xiri::NchfChargingDataRequest nchf;
    nchf.offline = offline;
    nchf.charging_data = xiri::SbiType{offline ? kOfflineRef : kConvergedRef, body};
    nchf.charging_event = event;
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
        nchf.roamer_in_out = *roamer_in_out;
        rec.information = nchf;
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

void ChfLiPoi::report_rf_acr(const std::vector<std::uint8_t>& message,
                             const std::vector<diameter_core::Avp>& avps) {
    namespace d = diameter_core;
    // Identities (clause 7.19.3.9: "the target identity as the User Name, or Subscriber Identifier
    // in the Service Information"): User-Name (RFC 6733, code 1) and every Subscription-Id (RFC
    // 4006, code 443) inside Service-Information (TS 32.299 7.2.192, code 873, vendor 10415).
    std::vector<li_poi::Match> found;
    const auto add = [&](std::vector<li_poi::Match> more) {
        for (auto& m : more) {
            if (std::none_of(
                    found.begin(), found.end(), [&](const auto& o) { return o.xid == m.xid; })) {
                found.push_back(std::move(m));
            }
        }
    };
    if (const auto* user_name = d::find_avp(avps, kUserName); user_name != nullptr) {
        const std::string value(user_name->data.begin(), user_name->data.end());
        add(impl_->runtime.matches(value, kSupportedKinds));
    }
    std::optional<xiri::RfChargingEvent> event;
    if (const auto* si = d::find_avp(avps, kServiceInformation, k3gppVendorId); si != nullptr) {
        if (const auto inner = d::decode_avps(si->data)) {
            for (const auto& avp : *inner) {
                if (avp.code == d::dictionary::Dcc::kSubscriptionId && avp.vendor_id == 0) {
                    add(impl_->match_subscription_id(avp));
                }
            }
            // TS 33.128 table 7.19.3.12-1 from the *-Information AVP present (TS 32.299 codes;
            // M2M-Information(15) is a oneM2M AVP whose code TS 32.299 does not give -- unmapped).
            for (const auto& [code, value] : kRfServiceInfo) {
                if (d::find_avp(*inner, code, k3gppVendorId) != nullptr) {
                    event = value;
                    break;
                }
            }
        }
    }
    if (found.empty()) {
        return;
    }
    xiri::ChargingDataEvent rec;
    // Location is C ("when it is included within a charging data request"): on Rf it would be the
    // binary 3GPP-User-Location-Info inside PS-Information, which this POI does not decode --
    // omitted, disclosed (ADR-0488).
    rec.information = xiri::RfChargingDataRequest{message, event};
    std::optional<std::vector<std::uint8_t>> payload;
    for (const auto& m : found) {
        const auto task = impl_->runtime.task(m.xid);
        if (!task || !task->charging_data_events) {
            continue;
        }
        if (*task->charging_data_events ==
            x1::ChargingDataEventsGenerated::TranslatedChargingDataInformation) {
            spdlog::error("chf-li-poi: task {} asks for TranslatedChargingDataInformation, which "
                          "this POI does not build -- ChargingDataEvent not sent (ADR-0488)",
                          m.xid);
            continue;
        }
        if (!payload) {
            auto encoded = xiri::encode_xiri_payload(rec);
            if (!encoded) {
                spdlog::error("chf-li-poi: could not encode ChargingDataEvent (Rf): {}",
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
