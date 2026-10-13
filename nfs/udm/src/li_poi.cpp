#include "li_poi.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <mutex>
#include <regex>
#include <unordered_set>

namespace udm {

namespace xiri = li_core::xiri;
namespace x1 = li_core::x1;

namespace {

// Same SBI-identity-scheme stripper every POI in this project carries (nfs/amf/src/li_poi.cpp,
// nfs/smf/src/li_poi.cpp) -- applied to both a warrant's target value and a queried identity
// before comparing (the runtime's own `normalise` hook does this for runtime.matches() calls; this
// copy is for the registered-target registry, which is not routed through the runtime).
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

// TS 29.571 Pei "imei-<15 digits>" carries the check digit; the xIRI's IMEI (NumericString
// SIZE(14)) does not. IMEISV is 16 digits in both -- same rule nfs/smf/src/li_poi.cpp uses.
std::string pei_for_matching(const std::string& pei) {
    std::string bare = bare_identity(pei);
    if (starts_with(pei, "imei-") && bare.size() == 15) {
        bare.pop_back();
    }
    return bare;
}

const std::vector<x1::TargetIdentifierKind>& supi_kinds() {
    using K = x1::TargetIdentifierKind;
    static const std::vector<K> kinds{K::SupiImsi, K::SupiNai};
    return kinds;
}
const std::vector<x1::TargetIdentifierKind>& pei_kinds() {
    using K = x1::TargetIdentifierKind;
    static const std::vector<K> kinds{K::PeiImei, K::PeiImeisv};
    return kinds;
}
const std::vector<x1::TargetIdentifierKind>& gpsi_kinds() {
    using K = x1::TargetIdentifierKind;
    static const std::vector<K> kinds{K::GpsiMsisdn, K::GpsiNai};
    return kinds;
}

// TS 33.127 7.2.2.2 names SUPI, PEI, GPSI and IMPU/IMPI as the UDM's target identity kinds; IMPU/
// IMPI have no TargetIdentifierKind anywhere in this project's X1 codec yet (a pre-existing gap of
// the shared codec, not something this ADR introduces or closes) -- disclosed, not modelled here.
const std::vector<x1::TargetIdentifierKind>& supported_kinds() {
    using K = x1::TargetIdentifierKind;
    static const std::vector<K> kinds{
        K::SupiImsi, K::SupiNai, K::PeiImei, K::PeiImeisv, K::GpsiMsisdn, K::GpsiNai};
    return kinds;
}

xiri::Supi to_xiri_supi(const std::string& supi) {
    const std::string bare = bare_identity(supi);
    return starts_with(supi, "nai-") ? xiri::Supi{xiri::Nai{bare}} : xiri::Supi{xiri::Imsi{bare}};
}

std::optional<xiri::Pei> to_xiri_pei(const std::optional<std::string>& pei) {
    if (!pei) {
        return std::nullopt;
    }
    const std::string v = pei_for_matching(*pei);
    if (v.size() == 16) {
        return xiri::Pei{xiri::Imeisv{v}};
    }
    if (v.size() == 14) {
        return xiri::Pei{xiri::Imei{v}};
    }
    return std::nullopt; // not an IMEI / IMEISV: not modelled, same disclosed scope as the SMF POI
}

std::optional<xiri::Gpsi> to_xiri_gpsi(const std::optional<std::string>& gpsi) {
    if (!gpsi) {
        return std::nullopt;
    }
    const std::string bare = bare_identity(*gpsi);
    if (starts_with(*gpsi, "msisdn-")) {
        return xiri::Gpsi{xiri::Msisdn{bare}};
    }
    return xiri::Gpsi{xiri::Nai{bare}};
}

// TS 33.501 Annex A.1's exact Serving Network Name string: "5G:mnc<3 digits>.mcc<3 digits>.
// 3gppnetwork.org" (a 2-digit MNC is zero-padded to 3 in this representation; nfs/amf/src/
// ngap_task.cpp's kServingNetworkName is this exact format). Anything else is a format this UDM
// does not parse -- the event is skipped (logged), never a guessed PLMNID.
std::optional<xiri::Plmnid> plmnid_from_snn(const std::string& snn) {
    static const std::regex re(R"(^5G:mnc(\d{3})\.mcc(\d{3})\.3gppnetwork\.org$)");
    std::smatch m;
    if (!std::regex_match(snn, m, re)) {
        return std::nullopt;
    }
    return xiri::Plmnid{m[2].str(), m[1].str()};
}

} // namespace

struct UdmLiPoi::Impl {
    Impl(li_poi::Config cfg, SubscriptionDataFetcher fetch)
        : runtime(std::move(cfg), make_hooks()), fetch_subscription_data(std::move(fetch)) {}

    li_poi::Hooks make_hooks() {
        li_poi::Hooks h;
        h.log_name = "udm-li-poi";
        h.supported_kinds = supported_kinds();
        h.normalise = [](const std::string& s) { return bare_identity(s); };
        h.on_targets_added = [this](const std::string& xid,
                                    const std::vector<x1::TargetIdentifier>& added) {
            start_of_interception(xid, added);
        };
        return h;
    }

    // One Match per task (two warrants on one subscriber are each reported under their own XID).
    std::vector<li_poi::Match> matches(const LiIdentities& ids) const {
        std::vector<li_poi::Match> out;
        const auto add = [&](std::vector<li_poi::Match> found) {
            for (auto& m : found) {
                const bool dup = std::any_of(
                    out.begin(), out.end(), [&](const auto& o) { return o.xid == m.xid; });
                if (!dup) {
                    out.push_back(std::move(m));
                }
            }
        };
        if (!ids.supi.empty()) {
            add(runtime.matches(ids.supi, supi_kinds()));
        }
        if (ids.pei) {
            add(runtime.matches(pei_for_matching(*ids.pei), pei_kinds()));
        }
        if (ids.gpsi) {
            add(runtime.matches(*ids.gpsi, gpsi_kinds()));
        }
        return out;
    }

    void emit(const li_poi::Match& match,
              const xiri::Event& event,
              li_core::PayloadDirection direction,
              const char* record) {
        const auto payload = xiri::encode_xiri_payload(event);
        if (!payload) {
            spdlog::error("udm-li-poi: could not encode {} xIRI: {}", record, payload.error());
            return;
        }
        runtime.emit(match, *payload, direction, record);
    }

    // TS 33.128 7.2.2.3.9: a warrant newly activated on a SUPI that is already registered. Runs on
    // the runtime's worker thread, off the X1 request path. Disclosed scope: only SUPI-kind
    // targets are checked (the registry is keyed by SUPI; a PEI/GPSI-only target cannot trigger
    // this in this build, no live PEI/GPSI -> SUPI resolution exists at hook time).
    void start_of_interception(const std::string& xid,
                               const std::vector<x1::TargetIdentifier>& added) {
        for (const auto& target : added) {
            if (target.kind != x1::TargetIdentifierKind::SupiImsi &&
                target.kind != x1::TargetIdentifierKind::SupiNai) {
                continue;
            }
            const std::string bare = bare_identity(target.value);
            {
                const std::lock_guard<std::mutex> lock(registry_mutex);
                if (registered.find(bare) == registered.end()) {
                    continue; // not registered: nothing to start with
                }
            }
            const auto task = runtime.task(xid);
            if (!task) {
                return; // deactivated before the worker got here
            }
            // X1 target values are bare digits (TS 103 221-1's own encoding, not an SBI scheme),
            // but the real UDR store and the real SDM resource path are both keyed by the TS
            // 29.571 SBI form ("imsi-"/"nai-" prefixed) -- reconstruct it before calling either.
            const std::string sbi_supi =
                (target.kind == x1::TargetIdentifierKind::SupiNai ? "nai-" : "imsi-") + bare;
            const auto subscription =
                fetch_subscription_data ? fetch_subscription_data(sbi_supi) : std::nullopt;
            if (!subscription) {
                spdlog::warn("udm-li-poi: task {} targets registered SUPI {} but am-data could "
                             "not be fetched -- UDMStartOfInterceptionWithRegisteredTarget "
                             "skipped, not fabricated",
                             xid,
                             bare);
                continue;
            }
            xiri::UdmStartOfInterceptionWithRegisteredTarget soi;
            soi.supi = to_xiri_supi(target.value);
            soi.sbi_reference = "/nudm-sdm/v2/" + sbi_supi + "/am-data";
            soi.sbi_value = *subscription;
            // 7.2.2.3.9 gives no explicit Payload Direction sentence, but every other
            // StartOfInterception* record in this spec (AMF/SMF) is explicitly Direction Value 5 --
            // applied here by the same precedent, disclosed.
            emit(li_poi::Match{xid, target, std::nullopt},
                 soi,
                 li_core::PayloadDirection::NotApplicable,
                 "UDMStartOfInterceptionWithRegisteredTarget");
        }
    }

    li_poi::PoiRuntime runtime;
    SubscriptionDataFetcher fetch_subscription_data;
    std::mutex registry_mutex;
    std::unordered_set<std::string> registered; // bare SUPI digits/NAI
};

UdmLiPoi::UdmLiPoi(li_poi::Config config, SubscriptionDataFetcher fetch_subscription_data)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(fetch_subscription_data))) {}
UdmLiPoi::~UdmLiPoi() = default;

void UdmLiPoi::start() {
    impl_->runtime.start();
}
void UdmLiPoi::stop() {
    impl_->runtime.stop();
}

void UdmLiPoi::note_registered(const std::string& supi) {
    const std::lock_guard<std::mutex> lock(impl_->registry_mutex);
    impl_->registered.insert(bare_identity(supi));
}

void UdmLiPoi::note_deregistered(const std::string& supi) {
    const std::lock_guard<std::mutex> lock(impl_->registry_mutex);
    impl_->registered.erase(bare_identity(supi));
}

void UdmLiPoi::report_serving_system(const LiIdentities& ids,
                                     xiri::UdmServingSystemMethod method,
                                     const std::optional<xiri::Plmnid>& plmn_id,
                                     std::optional<bool> roaming_indicator) {
    for (const auto& m : impl_->matches(ids)) {
        xiri::UdmServingSystemMessage msg;
        msg.supi = to_xiri_supi(ids.supi);
        msg.pei = to_xiri_pei(ids.pei);
        msg.gpsi = to_xiri_gpsi(ids.gpsi);
        msg.plmn_id = plmn_id;
        msg.serving_system_method = method;
        msg.roaming_indicator = roaming_indicator;
        // 7.2.2.3.2: explicitly Direction Value 5 (not applicable).
        impl_->emit(m, msg, li_core::PayloadDirection::NotApplicable, "UDMServingSystemMessage");
    }
}

void UdmLiPoi::report_cancel_location(const LiIdentities& ids,
                                      const std::optional<xiri::Plmnid>& plmn_id,
                                      xiri::UdmCancelLocationMethod method) {
    for (const auto& m : impl_->matches(ids)) {
        xiri::UdmCancelLocationMessage msg;
        msg.supi = to_xiri_supi(ids.supi);
        msg.pei = to_xiri_pei(ids.pei);
        msg.gpsi = to_xiri_gpsi(ids.gpsi);
        msg.plmn_id = plmn_id;
        msg.cancel_location_method = method;
        // 7.2.2.3.4: explicitly Direction Value 5 (not applicable).
        impl_->emit(m, msg, li_core::PayloadDirection::NotApplicable, "UDMCancelLocationMessage");
    }
}

void UdmLiPoi::report_ue_authentication_response(const std::string& supi,
                                                 const std::string& serving_network_name,
                                                 xiri::PrimaryAuthenticationType auth_type,
                                                 std::optional<bool> akma_indicator) {
    const LiIdentities ids{supi, std::nullopt, std::nullopt};
    for (const auto& m : impl_->matches(ids)) {
        const auto plmn = plmnid_from_snn(serving_network_name);
        if (!plmn) {
            spdlog::warn("udm-li-poi: servingNetworkName '{}' is not the TS 33.501 Annex A.1 SNN "
                         "format -- UDMUEAuthenticationResponse skipped, not fabricated",
                         serving_network_name);
            continue;
        }
        xiri::UdmUeAuthenticationResponse msg;
        msg.supi = to_xiri_supi(supi);
        msg.authentication_info_request.info_request_type = xiri::UdmInfoRequestType::Ausf;
        msg.authentication_info_request.auth_type = auth_type;
        msg.authentication_info_request.serving_network_name = *plmn;
        msg.authentication_info_request.rg_auth_ctx_supis = {to_xiri_supi(supi)};
        msg.akma_indicator = akma_indicator;
        // No explicit Payload Direction sentence in 7.2.2.3.8; this is a network-internal
        // AUSF<->UDM SBI exchange, not a message to/from the target UE, so Direction Value 5 --
        // the same disclosed interpretation ADR-0440 already applied to AmfLocationUpdate.
        impl_->emit(
            m, msg, li_core::PayloadDirection::NotApplicable, "UDMUEAuthenticationResponse");
    }
}

} // namespace udm
