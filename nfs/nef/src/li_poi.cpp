#include "li_poi.hpp"

#include <spdlog/spdlog.h>

#include <vector>

namespace nef {

namespace xiri = li_core::xiri;
namespace x1 = li_core::x1;

namespace {

// Same SBI-identity-scheme stripper every POI in this project carries.
std::string bare_identity(const std::string& s) {
    for (const char* prefix : {"imsi-", "nai-", "supi-", "msisdn-", "extid-"}) {
        const std::size_t len = std::char_traits<char>::length(prefix);
        if (s.size() >= len && s.compare(0, len, prefix) == 0) {
            return s.substr(len);
        }
    }
    return s;
}

using K = x1::TargetIdentifierKind;
const std::vector<K> kGpsiKinds{K::GpsiMsisdn, K::GpsiNai};
// TS 33.128 clause 7.7.1.2 (CAALevelUAVID applies only to a NEF acting as the UAS NF, which this
// one is not).
const std::vector<K> kSupportedKinds{K::SupiImsi, K::SupiNai, K::GpsiMsisdn, K::GpsiNai};

// The SBIReference strings exactly as table 7.7.6.1.2-1 gives them.
constexpr const char* kSubscriptionRef =
    "TS29122_AsSessionWithQoS.yaml#/components/schemas/AsSessionWithQoSSubscription";
constexpr const char* kPatchRef =
    "TS29122_AsSessionWithQoS.yaml#/components/schemas/AsSessionWithQoSSubscriptionPatch";

} // namespace

struct NefLiPoi::Impl {
    explicit Impl(li_poi::Config cfg) : runtime(std::move(cfg), make_hooks()) {}

    static li_poi::Hooks make_hooks() {
        li_poi::Hooks h;
        h.log_name = "nef-li-poi";
        h.supported_kinds = kSupportedKinds;
        h.normalise = [](const std::string& s) { return bare_identity(s); };
        return h;
    }

    li_poi::PoiRuntime runtime;
};

NefLiPoi::NefLiPoi(li_poi::Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}
NefLiPoi::~NefLiPoi() = default;

void NefLiPoi::start() {
    impl_->runtime.start();
}
void NefLiPoi::stop() {
    impl_->runtime.stop();
}

void NefLiPoi::report_af_session_with_qos_provision(
    const std::string& gpsi,
    const std::string& af_id,
    xiri::AfSessionWithQosOpType op,
    const std::optional<nlohmann::json>& subscription,
    const std::optional<nlohmann::json>& patch,
    int http_status) {
    const auto found = impl_->runtime.matches(gpsi, kGpsiKinds);
    if (found.empty()) {
        return;
    }
    const auto code = xiri::to_af_session_response_code(http_status);
    if (!code) {
        // aFSessionWithQoSResponseCode is mandatory and its ENUMERATED has no value for this
        // status -- the record is not sent rather than carrying a different code.
        spdlog::error("nef-li-poi: HTTP {} has no AForASSessionWithQoSResponseCode value -- "
                      "NEFAFSessionWithQoSProvision not sent",
                      http_status);
        return;
    }
    xiri::NefAfSessionWithQosProvision rec;
    const std::string bare = bare_identity(gpsi);
    rec.gpsi = gpsi.rfind("msisdn-", 0) == 0 ? xiri::Gpsi{xiri::Msisdn{bare}}
                                             : xiri::Gpsi{xiri::Nai{bare}};
    rec.af_id = af_id;
    rec.op_type = op;
    // Table 7.7.6.1.2-1: the subscription only for POST / PUT, the patch only for PATCH.
    if (subscription &&
        (op == xiri::AfSessionWithQosOpType::Post || op == xiri::AfSessionWithQosOpType::Put)) {
        rec.subscription = xiri::SbiType{kSubscriptionRef, subscription->dump()};
    }
    if (patch && op == xiri::AfSessionWithQosOpType::Patch) {
        rec.subscription_patch = xiri::SbiType{kPatchRef, patch->dump()};
    }
    rec.response_code = *code;

    const auto payload = xiri::encode_xiri_payload(rec);
    if (!payload) {
        spdlog::error("nef-li-poi: could not encode NEFAFSessionWithQoSProvision: {}",
                      payload.error());
        return;
    }
    for (const auto& m : found) {
        impl_->runtime.emit(
            m, *payload, li_core::PayloadDirection::NotApplicable, "NEFAFSessionWithQoSProvision");
    }
}

} // namespace nef
