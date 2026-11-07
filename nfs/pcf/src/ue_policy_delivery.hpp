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
inline constexpr std::uint8_t kUePolicyProvisioningRequest = 0x05;
inline constexpr std::uint8_t kUePolicyProvisioningReject = 0x06;

// UPDS cause values the PCF sends in a UE POLICY PROVISIONING REJECT (TS 24.587 §5.3.2.4).
inline constexpr std::uint8_t kUpdsRequestRejectedUnspecified = 31;
inline constexpr std::uint8_t kUpdsServiceOptionNotSupported = 32;
inline constexpr std::uint8_t kUpdsServiceOptionTemporarilyOutOfOrder = 34;
inline constexpr std::uint8_t kUpdsPtiAlreadyInUse = 35;
inline constexpr std::uint8_t kUpdsInvalidMandatoryInformation = 96;

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

// P6a: UE POLICY PROVISIONING REQUEST (TS 24.587 §7.2.1): PTI, message type, Requested UE
// policies (§8.3.2, LV, 2-4 octets). Only V2XPC5I, V2XUUI (octet 3 bits 1, 2) and A2XI (octet 4
// bit 2) are read; the 5G ProSe and RSLPP indications are ignored (ADR-0563 simplification).
struct ProvisioningRequest {
    std::uint8_t pti = 0;
    bool v2x_pc5 = false;
    bool v2x_uu = false;
    bool a2x = false;
};
// Error when the message is not a REQUEST, or the Requested UE policies IE is absent, out of its
// 1-3 octet value range or truncated (answered with UPDS cause #96).
tl::expected<ProvisioningRequest, std::string> parse_provisioning_request(const Bytes& msg);

// UE POLICY PROVISIONING REJECT (TS 24.587 §7.2.2): PTI, message type, UPDS cause.
Bytes build_provisioning_reject(std::uint8_t pti, std::uint8_t cause);

// Which info types to send: everything the association is authorized for (P4), or what the UE
// asked for (P6a: PC5 -> V2XP info 1, Uu -> V2XP info 2, A2XI -> both A2XP infos). TS 24.577
// §5.3.2.2 has the UE ask for A2X over PC5, over Uu or both, but the IE it uses (TS 24.587
// V19.3.0 §8.3.2) has the single A2XI bit, so both are sent (ADR-0563 spec inconsistency).
struct PolicySelection {
    bool v2x_pc5 = false;
    bool v2x_uu = false;
    bool a2x = false;
};

// V2XP and A2XP parts from TS 29.519 ServiceParameterData entries. Each base64 string becomes
// one info; a string that is not base64 is skipped and logged. Empty when there is nothing.
std::vector<Bytes> parts_from_service_params(const std::vector<nlohmann::json>& params,
                                             const PolicySelection& sel);
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
    bool pending_requested = false; // pending_pti came from a UE POLICY PROVISIONING REQUEST
    std::optional<std::uint8_t> last_result; // message type of the UE's last answer
    // P7: the AMF reported the UE unreachable for a COMMAND (N1N2 transfer failure, or a 409 /
    // 504 answer); re-delivered when the AMF reports CON_STATE_CH to CONNECTED (TS 23.502
    // §4.2.4.3).
    bool delivery_failure_pending = false;
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
    // P7: sets / clears delivery_failure_pending; false when the association is gone.
    bool set_delivery_failure(const std::string& pol_asso_id, bool pending);
    // P6a: true when the PTI is pending on a network-initiated COMMAND of the association
    // (answered with #35). A PTI pending from an earlier request of the UE is a retransmission
    // (TS 24.587 §5.3.2.6, T5040) and is not "in use".
    bool pti_in_use(const std::string& pol_asso_id, std::uint8_t pti) const;
    // P6a: records the request's PTI as the pending one (TS 24.501 D.1.2: the COMMAND carries
    // it). Replaces any other pending PTI of the association; false when it is gone.
    bool claim_requested_pti(const std::string& pol_asso_id, std::uint8_t pti);
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
// AMF N1N2MessageTransfer failure notification (TS 29.518 n1n2FailureTxfNotifURI), P7.
std::string n1n2_failure_path(const std::string& pol_asso_id);
inline constexpr std::string_view kNotifyRoot = "/npcf-ue-policy-control/v1/callbacks";

// kNotDelivered: the AMF answered N1N2MessageTransfer with 409 or 504, read here as "UE not
// reachable now" (simplification: the N1N2MessageTransferError cause is not inspected).
enum class DeliveryResult { kSent, kNothingToSend, kFailed, kRejected, kNotDelivered };

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

// P6a: answers a UE POLICY PROVISIONING REQUEST with a MANAGE UE POLICY COMMAND carrying the
// request's PTI and only the requested info types (TS 24.587 §5.3.2.3), or with a UE POLICY
// PROVISIONING REJECT (§5.3.2.4): #35 PTI pending on a network-initiated COMMAND, #32 nothing
// requested or not authorized (pc5Capab / a2xCapab), #34 the UDR read failed, #31 no parameters.
// Blocking: run it on the PCF's bounded worker.
DeliveryResult answer_provisioning_request(DeliveryDeps& deps,
                                           DeliveryStore& store,
                                           const std::string& pol_asso_id,
                                           const ProvisioningRequest& req);

// P6a: sends a UE POLICY PROVISIONING REJECT through N1N2MessageTransfer. A failed transfer
// aborts the procedure (TS 24.587 §5.3.2.5): logged, no state kept. Blocking.
DeliveryResult send_provisioning_reject(DeliveryDeps& deps,
                                        const std::string& supi,
                                        std::uint8_t pti,
                                        std::uint8_t cause);

} // namespace pcf::updp
