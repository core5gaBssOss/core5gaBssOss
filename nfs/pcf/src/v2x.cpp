// PCF V2X / A2X UE policy helpers (ADR-0551). See v2x.hpp for the spec references.
#include "v2x.hpp"

#include "sbi_core/multipart.hpp"

#include <spdlog/spdlog.h>

#include <PC5FlowBitRates.h>
#include <PC5QoSFlowItem.h>
#include <PC5QoSFlowList.h>
#include <PC5QoSParameters.h>
#include <Range.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <string_view>
#include <utility>

#include "ngap_core/ngap_codec.hpp"

namespace pcf::v2x {
namespace {

int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

std::string feature_string(std::initializer_list<unsigned> features) {
    unsigned max_feature = 0;
    for (const auto f : features)
        max_feature = std::max(max_feature, f);
    std::string out((max_feature + 3) / 4, '0');
    for (const auto f : features) {
        const std::size_t from_end = (f - 1) / 4;
        const std::size_t idx = out.size() - 1 - from_end;
        int v = hex_value(out[idx]) | (1 << ((f - 1) % 4));
        out[idx] = "0123456789ABCDEF"[v];
    }
    return out;
}

bool authorized(const nlohmann::json& parent, const char* group, const char* field) {
    const auto g = parent.find(group);
    if (g == parent.end() || !g->is_object())
        return false;
    const auto f = g->find(field);
    return f != g->end() && f->is_string() && f->get<std::string>() == "AUTHORIZED";
}

// TS 38.413 Range ::= ENUMERATED {m50, m80, m180, m200, m350, m400, m500, m700, m1000, ...}
constexpr std::array<std::pair<unsigned, long>, 9> kRanges{{{50, Range_m50},
                                                            {80, Range_m80},
                                                            {180, Range_m180},
                                                            {200, Range_m200},
                                                            {350, Range_m350},
                                                            {400, Range_m400},
                                                            {500, Range_m500},
                                                            {700, Range_m700},
                                                            {1000, Range_m1000}}};

std::optional<long> range_enum(unsigned metres) {
    for (const auto& [m, e] : kRanges)
        if (m == metres)
            return e;
    return std::nullopt;
}

struct Pc5Guard {
    PC5QoSParameters_t* p;
    ~Pc5Guard() { ASN_STRUCT_FREE_CONTENTS_ONLY(asn_DEF_PC5QoSParameters, p); }
};

} // namespace

bool has_feature(const std::string& supp_feat, unsigned feature) {
    if (feature == 0)
        return false;
    const std::size_t from_end = (feature - 1) / 4;
    if (from_end >= supp_feat.size())
        return false;
    const char c = supp_feat[supp_feat.size() - 1 - from_end];
    return (hex_value(c) >> ((feature - 1) % 4)) & 1;
}

std::string pcf_supported_features() {
    return feature_string({kFeaturePlmnChange, kFeatureV2x, kFeatureA2x});
}

std::string intersect_features(const std::string& a, const std::string& b) {
    const std::size_t n = std::min(a.size(), b.size());
    std::string out(n, '0');
    for (std::size_t i = 0; i < n; ++i) {
        const int v = hex_value(a[a.size() - n + i]) & hex_value(b[b.size() - n + i]);
        out[i] = "0123456789ABCDEF"[v];
    }
    return out;
}

bool v2x_eligible(const nlohmann::json& d) {
    if (!d.is_object())
        return false;
    for (const char* group : {"nrV2xServicesAuth", "lteV2xServicesAuth"})
        if (authorized(d, group, "vehicleUeAuth") || authorized(d, group, "pedestrianUeAuth"))
            return true;
    return false;
}

bool a2x_eligible(const nlohmann::json& d) {
    if (!d.is_object())
        return false;
    return authorized(d, "nrA2xServicesAuth", "uavUeAuth") ||
           authorized(d, "lteA2xServicesAuth", "uavUeAuth");
}

tl::expected<Pc5Config, std::string> parse_pc5_config(const nlohmann::json& config) {
    Pc5Config cfg;
    const auto flows = config.find("v2x_pc5_qos_flows");
    if (flows != config.end()) {
        if (!flows->is_array())
            return tl::unexpected("v2x_pc5_qos_flows must be an array");
        for (const auto& f : *flows) {
            if (!f.is_object() || !f.contains("pqi") || !f["pqi"].is_number_unsigned())
                return tl::unexpected("each v2x_pc5_qos_flows entry needs an unsigned pqi");
            Pc5QosFlow flow;
            flow.pqi = f["pqi"].get<long>();
            if (flow.pqi > 255)
                return tl::unexpected("pqi must be 0..255 (TS 38.413 FiveQI)");
            const bool has_g = f.contains("gfbr_bps"), has_m = f.contains("mfbr_bps");
            if (has_g != has_m)
                return tl::unexpected("gfbr_bps and mfbr_bps must be given together");
            if (has_g) {
                if (!f["gfbr_bps"].is_number_unsigned() || !f["mfbr_bps"].is_number_unsigned())
                    return tl::unexpected("gfbr_bps and mfbr_bps must be unsigned integers");
                flow.gfbr_bps = f["gfbr_bps"].get<std::uint64_t>();
                flow.mfbr_bps = f["mfbr_bps"].get<std::uint64_t>();
            }
            if (f.contains("range_m")) {
                if (!f["range_m"].is_number_unsigned() || !range_enum(f["range_m"].get<unsigned>()))
                    return tl::unexpected(
                        "range_m must be one of 50, 80, 180, 200, 350, 400, 500, 700, 1000");
                flow.range_m = f["range_m"].get<unsigned>();
            }
            cfg.flows.push_back(flow);
        }
    }
    const auto ambr = config.find("pc5_link_aggregate_bit_rate_bps");
    if (ambr != config.end()) {
        if (!ambr->is_number_unsigned())
            return tl::unexpected("pc5_link_aggregate_bit_rate_bps must be unsigned");
        cfg.link_aggregate_bit_rate_bps = ambr->get<std::uint64_t>();
    }
    return cfg;
}

tl::expected<std::vector<std::uint8_t>, std::string>
encode_pc5_qos_parameters(const Pc5Config& cfg) {
    if (cfg.flows.empty())
        return tl::unexpected("no PC5 QoS flows configured (v2x_pc5_qos_flows)");
    PC5QoSParameters_t params{};
    Pc5Guard guard{&params};
    for (const auto& f : cfg.flows) {
        auto* item = static_cast<PC5QoSFlowItem_t*>(std::calloc(1, sizeof(PC5QoSFlowItem_t)));
        if (item == nullptr)
            return tl::unexpected("out of memory");
        if (ASN_SEQUENCE_ADD(&params.pc5QoSFlowList.list, item) != 0) {
            std::free(item);
            return tl::unexpected("out of memory");
        }
        item->pQI = f.pqi;
        if (f.gfbr_bps) {
            item->pc5FlowBitRates =
                static_cast<PC5FlowBitRates*>(std::calloc(1, sizeof(PC5FlowBitRates_t)));
            if (item->pc5FlowBitRates == nullptr)
                return tl::unexpected("out of memory");
            if (asn_umax2INTEGER(&item->pc5FlowBitRates->guaranteedFlowBitRate, *f.gfbr_bps) != 0 ||
                asn_umax2INTEGER(&item->pc5FlowBitRates->maximumFlowBitRate, *f.mfbr_bps) != 0)
                return tl::unexpected("bit rate encoding failed");
        }
        if (f.range_m) {
            item->range = static_cast<Range_t*>(std::calloc(1, sizeof(Range_t)));
            if (item->range == nullptr)
                return tl::unexpected("out of memory");
            *item->range = *range_enum(*f.range_m);
        }
    }
    if (cfg.link_aggregate_bit_rate_bps) {
        params.pc5LinkAggregateBitRates =
            static_cast<BitRate_t*>(std::calloc(1, sizeof(BitRate_t)));
        if (params.pc5LinkAggregateBitRates == nullptr)
            return tl::unexpected("out of memory");
        if (asn_umax2INTEGER(params.pc5LinkAggregateBitRates, *cfg.link_aggregate_bit_rate_bps) !=
            0)
            return tl::unexpected("bit rate encoding failed");
    }
    auto bytes = ngap::encode_value(&asn_DEF_PC5QoSParameters, &params);
    if (bytes.empty())
        return tl::unexpected("APER encoding of PC5QoSParameters failed");
    return bytes;
}

nlohmann::json n1n2_v2x_request(const std::string& content_id,
                                const std::string& supported_features) {
    nlohmann::json j = {
        {"n2InfoContainer",
         {{"n2InformationClass", "V2X"},
          {"v2xInfo",
           {{"n2Pc5Pol",
             {{"ngapIeType", "PC5_QOS_PARA"}, {"ngapData", {{"contentId", content_id}}}}}}}}}};
    if (!supported_features.empty())
        j["supportedFeatures"] = supported_features;
    return j;
}

namespace {

// GET {udm}/nudm-sdm/v2/{supi}/{resource}; the JSON body on 200, nullopt on any failure.
std::optional<nlohmann::json>
get_udm(ProvisionDeps& d, const std::string& supi, const char* resource) {
    auto token = d.udm_oauth.get_bearer_token();
    if (!token.has_value()) {
        spdlog::warn("pcf: no UDM token for Nudm_SDM {} : {}", resource, token.error());
        return std::nullopt;
    }
    sbi_core::http2::ClientRequest r;
    r.method = "GET";
    r.url = d.udm_base_url + "/nudm-sdm/v2/" + encode_path_segment(supi) + "/" + resource;
    r.headers.emplace("authorization", "Bearer " + *token);
    const auto resp = d.udm_client.send(r);
    if (!resp.has_value()) {
        spdlog::warn("pcf: Nudm_SDM {} failed: {}", resource, resp.error());
        return std::nullopt;
    }
    if (resp->status != 200) {
        spdlog::warn("pcf: Nudm_SDM {} answered {}", resource, resp->status);
        return std::nullopt;
    }
    try {
        return std::optional<nlohmann::json>(std::in_place, nlohmann::json::parse(resp->body));
    } catch (const nlohmann::json::exception& e) {
        spdlog::warn("pcf: Nudm_SDM {} body is not JSON: {}", resource, e.what());
        return std::nullopt;
    }
}

} // namespace

std::string encode_path_segment(const std::string& segment) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(segment.size());
    for (const char ch : segment) {
        const auto c = static_cast<unsigned char>(ch);
        const bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') ||
                          std::string_view("-._~!$&'()*+,;=:@").find(static_cast<char>(c)) !=
                              std::string_view::npos;
        if (keep) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

ProvisionResult
provision_v2x(ProvisionDeps& d, const std::string& supi, const std::string& supp_feat) {
    const auto data = get_udm(d, supi, "v2x-data");
    if (!data) {
        return ProvisionResult::kFailed;
    }
    if (!v2x_eligible(*data)) {
        spdlog::info("pcf: UE {} not authorized for V2X; no PC5 policy sent", supi);
        return ProvisionResult::kNotEligible;
    }
    if (d.pc5.flows.empty()) {
        spdlog::info("pcf: UE {} is V2X-authorized but v2x_pc5_qos_flows is empty; nothing sent",
                     supi);
        return ProvisionResult::kNothingToSend;
    }
    const auto ngap = encode_pc5_qos_parameters(d.pc5);
    if (!ngap.has_value()) {
        spdlog::error("pcf: PC5QoSParameters encoding failed: {}", ngap.error());
        return ProvisionResult::kFailed;
    }
    auto token = d.amf_oauth.get_bearer_token();
    if (!token.has_value()) {
        spdlog::warn("pcf: no AMF token for N1N2MessageTransfer: {}", token.error());
        return ProvisionResult::kFailed;
    }
    static constexpr const char* kContentId = "n2Pc5Pol";
    sbi_core::multipart::Part json_part;
    json_part.content_type = "application/json";
    json_part.body = n1n2_v2x_request(kContentId, supp_feat).dump();
    sbi_core::multipart::Part bin_part;
    bin_part.content_type = "application/vnd.3gpp.ngap";
    bin_part.content_id = kContentId;
    bin_part.body.assign(ngap->begin(), ngap->end());
    const auto encoded = sbi_core::multipart::encode({json_part, bin_part});

    sbi_core::http2::ClientRequest r;
    r.method = "POST";
    r.url = d.amf_base_url + "/namf-comm/v1/ue-contexts/" + encode_path_segment(supi) +
            "/n1-n2-messages";
    r.headers.emplace("content-type", encoded.content_type_header);
    r.headers.emplace("authorization", "Bearer " + *token);
    r.body = encoded.body;
    const auto resp = d.amf_client.send(r);
    if (!resp.has_value()) {
        spdlog::warn("pcf: N1N2MessageTransfer for {} failed: {}", supi, resp.error());
        return ProvisionResult::kFailed;
    }
    // TS 29.518 YAML: 200 (transfer initiated / done) and 202 (accepted, e.g. paging) are success.
    if (resp->status != 200 && resp->status != 202) {
        spdlog::warn("pcf: N1N2MessageTransfer for {} answered {}", supi, resp->status);
        return ProvisionResult::kFailed;
    }
    spdlog::info("pcf: V2X N2 PC5 policy sent to the AMF for {} (status {})", supi, resp->status);
    return ProvisionResult::kSent;
}

ProvisionResult check_a2x(ProvisionDeps& d, const std::string& supi) {
    const auto data = get_udm(d, supi, "a2x-data");
    if (!data) {
        return ProvisionResult::kFailed;
    }
    if (!a2x_eligible(*data)) {
        spdlog::info("pcf: UE {} not authorized for A2X", supi);
        return ProvisionResult::kNotEligible;
    }
    // The A2X N2 PC5 policy (n2Pc5PolA2x) is deferred for future implementation (ADR-0551).
    spdlog::info("pcf: UE {} is A2X-authorized; the A2X N2 PC5 policy is deferred", supi);
    return ProvisionResult::kNothingToSend;
}

} // namespace pcf::v2x
