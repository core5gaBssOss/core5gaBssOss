// PCF V2X / A2X UE policy helpers (ADR-0551, increment 1: P1, P2, P5).
//
// Spec: TS 29.525 (Npcf_UEPolicyControl) §4.2.2.2, §4.2.2.3, §4.2.3.1, §5.8 (features);
// TS 23.287 §6.2.2 (V2X policy provisioning), TS 23.256 §6.2.7.1 (A2X), TS 23.502 §4.16.11.
// NGAP encoding of the N2 PC5 policy: TS 38.413 PC5QoSParameters (ngap_generated, asn1c).
#pragma once

#include "sbi_core/http2_client.hpp"
#include "sbi_core/oauth2_client.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <tl/expected.hpp>
#include <vector>

namespace pcf::v2x {

// TS 29.525 §5.8 feature numbers. Feature n is bit (n-1) of the SupportedFeatures hex string,
// counted from the least significant bit of its last character (TS 29.571 SupportedFeatures).
inline constexpr unsigned kFeaturePlmnChange = 2;
inline constexpr unsigned kFeatureV2x = 4;
inline constexpr unsigned kFeatureA2x = 15;

bool has_feature(const std::string& supp_feat, unsigned feature);

// The PCF's own SupportedFeatures for Npcf_UEPolicyControl: PlmnChange, V2X and A2X.
std::string pcf_supported_features();

// Bitwise AND of two SupportedFeatures strings (TS 29.500 §6.6.2). Characters that are not hex
// digits count as 0. The result has the length of the shorter input with leading zeros kept,
// or is empty when either input is empty.
std::string intersect_features(const std::string& a, const std::string& b);

// Eligibility from the UDM's V2xSubscriptionData (TS 29.503 v2x-data): AUTHORIZED for a vehicle
// or a pedestrian UE in nrV2xServicesAuth or lteV2xServicesAuth.
bool v2x_eligible(const nlohmann::json& v2x_subscription_data);
// Eligibility from the UDM's A2xSubscriptionData (TS 29.503 a2x-data): uavUeAuth AUTHORIZED in
// nrA2xServicesAuth or lteA2xServicesAuth.
bool a2x_eligible(const nlohmann::json& a2x_subscription_data);

// One configured PC5 QoS flow (config key v2x_pc5_qos_flows; guardrail 8).
struct Pc5QosFlow {
    long pqi = 0;
    std::optional<std::uint64_t> gfbr_bps;
    std::optional<std::uint64_t> mfbr_bps;
    std::optional<unsigned> range_m;
};

struct Pc5Config {
    std::vector<Pc5QosFlow> flows;
    std::optional<std::uint64_t> link_aggregate_bit_rate_bps;
};

// Parses v2x_pc5_qos_flows / pc5_link_aggregate_bit_rate_bps from the PCF config. A missing key
// gives an empty flow list. gfbr_bps and mfbr_bps are both present or both absent (TS 38.413
// PC5FlowBitRates has both as mandatory); range_m must be one of the TS 38.413 Range values.
tl::expected<Pc5Config, std::string> parse_pc5_config(const nlohmann::json& config);

// Encodes the TS 38.413 PC5QoSParameters IE (APER) for the configured flows.
tl::expected<std::vector<std::uint8_t>, std::string>
encode_pc5_qos_parameters(const Pc5Config& cfg);

// The JSON part of the N1N2MessageTransfer request (TS 29.518 N1N2MessageTransferReqData) that
// carries the V2X N2 PC5 policy (TS 29.525 §4.2.2.3): n2InformationClass V2X, v2xInfo.n2Pc5Pol
// with ngapIeType PC5_QOS_PARA and ngapData referring to the binary part content_id.
nlohmann::json n1n2_v2x_request(const std::string& content_id,
                                const std::string& supported_features);

// RFC 3986 §3.3 pchar percent-encoding of one path segment (TS 29.501 §5.2.3): unreserved,
// sub-delims, ':' and '@' are kept, every other byte becomes %XX. Duplicates
// sbi_core::uri::encode_path_segment from the SEC-15 branch (ADR-0549), which is not on main yet;
// to be replaced by it once that lands.
std::string encode_path_segment(const std::string& segment);

// What P5 (V2X/A2X policy provisioning, ADR-0551) needs to reach the UDM and the AMF.
struct ProvisionDeps {
    sbi_core::http2::Client& udm_client;
    sbi_core::OAuth2Client& udm_oauth;
    sbi_core::http2::Client& amf_client;
    sbi_core::OAuth2Client& amf_oauth;
    std::string udm_base_url;
    std::string amf_base_url;
    Pc5Config pc5;
};

// Outcome of one provisioning run, for logs and tests.
enum class ProvisionResult {
    kSent,          // N1N2MessageTransfer accepted by the AMF (200 or 202)
    kNotEligible,   // UDM subscription data does not authorize the UE
    kNothingToSend, // eligible, but no PC5 QoS flow is configured / A2X N2 part deferred
    kFailed,        // a token, the UDM or the AMF failed; nothing was provisioned (fail closed)
};

// P5: for a UE whose association negotiated V2X (and/or A2X), GET the UDM's v2x-data (a2x-data)
// (TS 29.503 Nudm_SDM GetV2xData / GetA2xData) and, when V2X is authorized, send the PC5 QoS
// parameters to the AMF as the N2 PC5 policy in Namf_Communication_N1N2MessageTransfer
// (TS 29.525 §4.2.2.3, TS 29.518). Blocking: run it on the PCF's bounded worker, never on an SBI
// server thread.
ProvisionResult
provision_v2x(ProvisionDeps& deps, const std::string& supi, const std::string& supp_feat);
ProvisionResult check_a2x(ProvisionDeps& deps, const std::string& supi);

} // namespace pcf::v2x
