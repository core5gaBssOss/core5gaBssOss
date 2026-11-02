// PCF V2XP / A2XP UE policy delivery over N1 (ADR-0555, ADR-0551 increment 2: P3, P4).
//
// P3: the V2X / A2X service parameters an AF provisioned through the NEF are read from the UDR
// application data (TS 29.519 serviceParamData: paramOverPc5, paramOverUu, a2xParamsPc5,
// a2xParamsUu) and the PCF subscribes to their changes (subs-to-notify, dataInd SVC_PARAM).
// P4: the PCF sends them to the UE in a MANAGE UE POLICY COMMAND (TS 24.501 Annex D.5.1) carrying
// a V2XP part (TS 24.588) and/or an A2XP part (TS 24.578), through
// Namf_Communication_N1N2MessageTransfer with N1 message class UPDP (TS 29.518), and takes the
// UE's MANAGE UE POLICY COMPLETE / COMMAND REJECT from the N1MessageNotify it subscribed to.
// Stage 2: TS 23.287 §6.2.2, TS 23.256 §6.2.7.1, TS 23.502 §4.2.4.3.
#pragma once

#include "sbi_core/http2_client.hpp"
#include "sbi_core/oauth2_client.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tl/expected.hpp>
#include <vector>

namespace pcf::updp {

using Bytes = std::vector<std::uint8_t>;

// TS 24.501 Annex D.6 UE policy delivery service message types.
inline constexpr std::uint8_t kManageUePolicyCommand = 0x01;
inline constexpr std::uint8_t kManageUePolicyComplete = 0x02;
inline constexpr std::uint8_t kManageUePolicyCommandReject = 0x03;

// TS 24.501 D.6.2 UE policy part types (low nibble of the type octet).
inline constexpr std::uint8_t kPartV2xp = 0x03;
inline constexpr std::uint8_t kPartA2xp = 0x05;

// V2XP info types (TS 24.588) and A2XP info types (TS 24.578) this build sends.
inline constexpr std::uint8_t kV2xpInfoPc5 = 1;
inline constexpr std::uint8_t kV2xpInfoUu = 2;
inline constexpr std::uint8_t kA2xpInfoPc5 = 1;
inline constexpr std::uint8_t kA2xpInfoUu = 5;

// The single UE policy section code this build uses for the V2X/A2X section (ADR-0555: a
// simplification -- one section per UE per PLMN, replaced on each delivery).
inline constexpr std::uint16_t kV2xSectionCode = 0x0001;

struct Plmn {
    std::string mcc; // 3 digits
    std::string mnc; // 2 or 3 digits
};

struct PolicyInfo {
    std::uint8_t type = 0;
    Bytes contents;
};

// RFC 4648 base64 (standard alphabet, padding optional); nullopt on any other character.
std::optional<Bytes> base64_decode(std::string_view in);

// TS 24.501 D.6.2 PLMN ID (3 octets, BCD, MNC digit 3 = 0xF for a 2-digit MNC).
tl::expected<Bytes, std::string> encode_plmn(const Plmn& plmn);

// One UE policy part: contents length (2 octets, covering the type octet and the contents,
// TS 24.501 Table D.6.2.1 NOTE 2), the part type, then each info as type, 2-octet length,
// contents (TS 24.588 / TS 24.578).
tl::expected<Bytes, std::string> make_policy_part(std::uint8_t part_type,
                                                  const std::vector<PolicyInfo>& infos);

// MANAGE UE POLICY COMMAND with one UE policy section management sublist (the given PLMN)
// holding one instruction (the given UPSC) with the given, already-encoded UE policy parts.
tl::expected<Bytes, std::string> build_manage_ue_policy_command(std::uint8_t pti,
                                                                const Plmn& plmn,
                                                                std::uint16_t upsc,
                                                                const std::vector<Bytes>& parts);

struct UePolicyMessage {
    std::uint8_t pti = 0;
    std::uint8_t type = 0;
};
// The PTI and message type of a UE policy delivery service message; error when too short.
tl::expected<UePolicyMessage, std::string> parse_ue_policy_message(const Bytes& msg);

// V2XP and A2XP parts from TS 29.519 ServiceParameterData entries. Each base64 string becomes
// one info; a string that is not base64 is skipped and logged. Empty when there is nothing.
std::vector<Bytes>
parts_from_service_params(const std::vector<nlohmann::json>& params, bool v2x, bool a2x);

// Per UE policy association delivery state (TS 24.501 D.2: the PTI in use until the UE answers).
struct Association {
    std::string supi;
    Plmn plmn;
    bool v2x = false;
    bool a2x = false;
    std::string udr_subscription; // Location of the UDR subs-to-notify resource
    std::string amf_subscription; // n1n2NotifySubscriptionId at the AMF
    std::optional<std::uint8_t> pending_pti;
    std::optional<std::uint8_t> last_result; // message type of the UE's last answer
};

// Thread-safe association table plus the PTI allocator over 0x80-0xFE (TS 24.501 D.2: values
// the network allocates; a value is not reused while it is pending on any association).
class DeliveryStore {
public:
    void put(const std::string& pol_asso_id, Association a);
    std::optional<Association> get(const std::string& pol_asso_id) const;
    std::optional<Association> take(const std::string& pol_asso_id);
    // Associations of one UE, for a UDR change notification (keyed by SUPI).
    std::vector<std::string> by_supi(const std::string& supi) const;
    // Allocates a free PTI for the association and records it as pending; nullopt when all 127
    // values are pending or the association is gone.
    std::optional<std::uint8_t> allocate_pti(const std::string& pol_asso_id);
    void release_pti(const std::string& pol_asso_id);
    void set_subscriptions(const std::string& pol_asso_id,
                           std::optional<std::string> udr,
                           std::optional<std::string> amf);
    // Applies a COMPLETE / REJECT for the association; false when its PTI is not the pending one.
    bool answer(const std::string& pol_asso_id, const UePolicyMessage& msg);

private:
    mutable std::mutex mutex_;
    std::map<std::string, Association> by_id_;
};

struct DeliveryDeps {
    sbi_core::http2::Client& udr_client;
    sbi_core::OAuth2Client& udr_oauth;
    sbi_core::http2::Client& amf_client;
    sbi_core::OAuth2Client& amf_oauth;
    std::string udr_base_url;
    std::string amf_base_url;
    std::string self_base_url;
    std::string nf_instance_id;
};

// Callback paths on the PCF (this project's choice of notification URIs).
std::string udr_notify_path();                              // UDR application data changes
std::string n1_notify_path(const std::string& pol_asso_id); // AMF N1MessageNotify (UPDP)
inline constexpr std::string_view kNotifyRoot = "/npcf-ue-policy-control/v1/callbacks";

enum class DeliveryResult { kSent, kNothingToSend, kFailed };

// P3: GET serviceParamData for the UE (supis query); entries for other SUPIs are dropped.
tl::expected<std::vector<nlohmann::json>, std::string>
fetch_service_params(DeliveryDeps& deps, const std::string& supi);

// P3 + P4 subscriptions; each is attempted once and stored when created.
void subscribe(DeliveryDeps& deps, DeliveryStore& store, const std::string& pol_asso_id);
// DELETE both subscriptions of a removed association (best effort, logged).
void unsubscribe(DeliveryDeps& deps, const Association& a);

// P3 + P4: read the parameters, build the command and send it with N1N2MessageTransfer.
// Blocking: run it on the PCF's bounded worker, never on an SBI server thread.
DeliveryResult deliver(DeliveryDeps& deps, DeliveryStore& store, const std::string& pol_asso_id);

} // namespace pcf::updp
