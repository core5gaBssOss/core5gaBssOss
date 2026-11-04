// PCF V2XP / A2XP UE policy delivery over N1 (ADR-0555). See ue_policy_delivery.hpp.
#include "ue_policy_delivery.hpp"

#include "sbi_core/multipart.hpp"
#include "sbi_core/uri.hpp"

#include <spdlog/spdlog.h>

#include <array>
#include <exception>
#include <utility>

#include "v2x.hpp"

namespace pcf::updp {
namespace {

int b64_value(char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

bool all_digits(const std::string& s) {
    for (const char c : s)
        if (c < '0' || c > '9')
            return false;
    return true;
}

void put_u16(Bytes& out, std::size_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

std::uint8_t digit(char c) {
    return static_cast<std::uint8_t>(c - '0');
}

// Collects the base64 strings of one field; a non-string or non-base64 value is skipped and logged.
void collect(const nlohmann::json& entry,
             const char* field,
             std::uint8_t info_type,
             std::vector<PolicyInfo>& out) {
    const auto it = entry.find(field);
    if (it == entry.end() || it->is_null())
        return;
    if (!it->is_string()) {
        spdlog::warn("pcf: serviceParamData {} is not a string; skipped", field);
        return;
    }
    auto bytes = base64_decode(it->get<std::string>());
    if (!bytes.has_value()) {
        spdlog::warn("pcf: serviceParamData {} is not base64; skipped", field);
        return;
    }
    if (bytes->empty())
        return;
    out.push_back(PolicyInfo{info_type, std::move(*bytes)});
}

} // namespace

std::optional<Bytes> base64_decode(std::string_view in) {
    while (!in.empty() && in.back() == '=')
        in.remove_suffix(1);
    Bytes out;
    out.reserve(in.size() * 3 / 4);
    std::uint32_t acc = 0;
    int bits = 0;
    for (const char c : in) {
        const int v = b64_value(c);
        if (v < 0)
            return std::nullopt;
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
        }
    }
    // A single leftover sextet cannot encode a whole octet (RFC 4648 §4).
    if (bits == 6)
        return std::nullopt;
    return out;
}

tl::expected<Bytes, std::string> encode_plmn(const Plmn& plmn) {
    if (plmn.mcc.size() != 3 || !all_digits(plmn.mcc))
        return tl::make_unexpected("MCC must be 3 digits: '" + plmn.mcc + "'");
    if ((plmn.mnc.size() != 2 && plmn.mnc.size() != 3) || !all_digits(plmn.mnc))
        return tl::make_unexpected("MNC must be 2 or 3 digits: '" + plmn.mnc + "'");
    const std::uint8_t mnc3 = plmn.mnc.size() == 3 ? digit(plmn.mnc[2]) : 0x0F;
    return Bytes{static_cast<std::uint8_t>((digit(plmn.mcc[1]) << 4) | digit(plmn.mcc[0])),
                 static_cast<std::uint8_t>((mnc3 << 4) | digit(plmn.mcc[2])),
                 static_cast<std::uint8_t>((digit(plmn.mnc[1]) << 4) | digit(plmn.mnc[0]))};
}

tl::expected<Bytes, std::string> make_policy_part(std::uint8_t part_type,
                                                  const std::vector<PolicyInfo>& infos) {
    if (part_type > 0x0F)
        return tl::make_unexpected("UE policy part type must fit in 4 bits");
    Bytes contents;
    for (const auto& info : infos) {
        if (info.contents.size() > 0xFFFF)
            return tl::make_unexpected("policy info longer than 65535 octets");
        contents.push_back(info.type);
        put_u16(contents, info.contents.size());
        contents.insert(contents.end(), info.contents.begin(), info.contents.end());
    }
    // The part length covers the type octet plus the contents (TS 24.501 Table D.6.2.1 NOTE 2).
    const std::size_t length = 1 + contents.size();
    if (length > 0xFFFF)
        return tl::make_unexpected("UE policy part longer than 65535 octets");
    Bytes out;
    put_u16(out, length);
    out.push_back(part_type); // high nibble spare = 0
    out.insert(out.end(), contents.begin(), contents.end());
    return out;
}

tl::expected<Bytes, std::string> build_manage_ue_policy_command(std::uint8_t pti,
                                                                const Plmn& plmn,
                                                                std::uint16_t upsc,
                                                                const std::vector<Bytes>& parts) {
    const auto plmn_id = encode_plmn(plmn);
    if (!plmn_id.has_value())
        return tl::make_unexpected(plmn_id.error());
    // Instruction: contents length (UPSC + parts, ADR-0555 assumption), UPSC, UE policy parts.
    Bytes instruction_body;
    put_u16(instruction_body, upsc);
    for (const auto& p : parts)
        instruction_body.insert(instruction_body.end(), p.begin(), p.end());
    if (instruction_body.size() > 0xFFFF)
        return tl::make_unexpected("instruction longer than 65535 octets");
    // Sublist: length (PLMN + instructions), PLMN ID, the one instruction.
    Bytes sublist_body = *plmn_id;
    put_u16(sublist_body, instruction_body.size());
    sublist_body.insert(sublist_body.end(), instruction_body.begin(), instruction_body.end());
    if (sublist_body.size() > 0xFFFF)
        return tl::make_unexpected("sublist longer than 65535 octets");
    Bytes list;
    put_u16(list, sublist_body.size());
    list.insert(list.end(), sublist_body.begin(), sublist_body.end());
    if (list.size() > 0xFFFF)
        return tl::make_unexpected("UE policy section management list longer than 65535 octets");
    // Message: PTI, message type, UE policy section management list (LV-E).
    Bytes msg{pti, kManageUePolicyCommand};
    put_u16(msg, list.size());
    msg.insert(msg.end(), list.begin(), list.end());
    return msg;
}

tl::expected<UePolicyMessage, std::string> parse_ue_policy_message(const Bytes& msg) {
    if (msg.size() < 2)
        return tl::make_unexpected("UE policy delivery message shorter than 2 octets");
    return UePolicyMessage{msg[0], msg[1]};
}

tl::expected<ProvisioningRequest, std::string> parse_provisioning_request(const Bytes& msg) {
    if (msg.size() < 2 || msg[1] != kUePolicyProvisioningRequest)
        return tl::make_unexpected("not a UE POLICY PROVISIONING REQUEST");
    // TS 24.007 Table 11.6: 00H is "no PTI assigned", FFH is reserved.
    if (msg[0] == 0x00 || msg[0] == 0xFF)
        return tl::make_unexpected("PTI 00H/FFH is not a valid request PTI");
    if (msg.size() < 3)
        return tl::make_unexpected("Requested UE policies IE is missing");
    const std::size_t len = msg[2];
    if (len < 1 || len > 3)
        return tl::make_unexpected("Requested UE policies length " + std::to_string(len) +
                                   " is outside 1-3");
    if (msg.size() < 3 + len)
        return tl::make_unexpected("Requested UE policies IE is truncated");
    ProvisioningRequest req;
    req.pti = msg[0];
    req.v2x_pc5 = (msg[3] & 0x01) != 0;
    req.v2x_uu = (msg[3] & 0x02) != 0;
    if (len >= 2)
        req.a2x = (msg[4] & 0x02) != 0;
    return req;
}

Bytes build_provisioning_reject(std::uint8_t pti, std::uint8_t cause) {
    return Bytes{pti, kUePolicyProvisioningReject, cause};
}

std::vector<Bytes> parts_from_service_params(const std::vector<nlohmann::json>& params,
                                             const PolicySelection& sel) {
    std::vector<PolicyInfo> v2x_infos;
    std::vector<PolicyInfo> a2x_infos;
    for (const auto& entry : params) {
        if (!entry.is_object())
            continue;
        if (sel.v2x_pc5)
            collect(entry, "paramOverPc5", kV2xpInfoPc5, v2x_infos);
        if (sel.v2x_uu)
            collect(entry, "paramOverUu", kV2xpInfoUu, v2x_infos);
        if (sel.a2x) {
            collect(entry, "a2xParamsPc5", kA2xpInfoPc5, a2x_infos);
            collect(entry, "a2xParamsUu", kA2xpInfoUu, a2x_infos);
        }
    }
    std::vector<Bytes> parts;
    for (auto [type, infos] :
         {std::pair{kPartV2xp, &v2x_infos}, std::pair{kPartA2xp, &a2x_infos}}) {
        if (infos->empty())
            continue;
        auto part = make_policy_part(type, *infos);
        if (!part.has_value()) {
            spdlog::warn("pcf: UE policy part {} not built: {}", type, part.error());
            continue;
        }
        parts.push_back(std::move(*part));
    }
    return parts;
}

std::vector<Bytes>
parts_from_service_params(const std::vector<nlohmann::json>& params, bool v2x, bool a2x) {
    return parts_from_service_params(params, PolicySelection{v2x, v2x, a2x});
}

void DeliveryStore::put(const std::string& pol_asso_id, Association a) {
    std::lock_guard lock(mutex_);
    by_id_[pol_asso_id] = std::move(a);
}

std::optional<Association> DeliveryStore::get(const std::string& pol_asso_id) const {
    std::lock_guard lock(mutex_);
    const auto it = by_id_.find(pol_asso_id);
    if (it == by_id_.end())
        return std::nullopt;
    return it->second;
}

std::optional<Association> DeliveryStore::take(const std::string& pol_asso_id) {
    std::lock_guard lock(mutex_);
    const auto it = by_id_.find(pol_asso_id);
    if (it == by_id_.end())
        return std::nullopt;
    Association a = std::move(it->second);
    by_id_.erase(it);
    return a;
}

std::vector<std::string> DeliveryStore::by_supi(const std::string& supi) const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> ids;
    for (const auto& [id, a] : by_id_)
        if (a.supi == supi)
            ids.push_back(id);
    return ids;
}

std::optional<std::uint8_t> DeliveryStore::allocate_pti(const std::string& pol_asso_id) {
    std::lock_guard lock(mutex_);
    const auto self = by_id_.find(pol_asso_id);
    if (self == by_id_.end())
        return std::nullopt;
    std::array<bool, 256> used{};
    for (const auto& [id, a] : by_id_)
        if (a.pending_pti.has_value())
            used[*a.pending_pti] = true;
    for (unsigned v = 0x80; v <= 0xFE; ++v) {
        if (!used[v]) {
            self->second.pending_pti = static_cast<std::uint8_t>(v);
            self->second.pending_requested = false;
            return static_cast<std::uint8_t>(v);
        }
    }
    return std::nullopt;
}

void DeliveryStore::release_pti(const std::string& pol_asso_id) {
    std::lock_guard lock(mutex_);
    const auto it = by_id_.find(pol_asso_id);
    if (it != by_id_.end()) {
        it->second.pending_pti.reset();
        it->second.pending_requested = false;
    }
}

bool DeliveryStore::set_delivery_failure(const std::string& pol_asso_id, bool pending) {
    std::lock_guard lock(mutex_);
    const auto it = by_id_.find(pol_asso_id);
    if (it == by_id_.end())
        return false;
    it->second.delivery_failure_pending = pending;
    return true;
}

bool DeliveryStore::pti_in_use(const std::string& pol_asso_id, std::uint8_t pti) const {
    std::lock_guard lock(mutex_);
    const auto it = by_id_.find(pol_asso_id);
    if (it == by_id_.end() || !it->second.pending_pti.has_value())
        return false;
    return *it->second.pending_pti == pti && !it->second.pending_requested;
}

bool DeliveryStore::claim_requested_pti(const std::string& pol_asso_id, std::uint8_t pti) {
    std::lock_guard lock(mutex_);
    const auto it = by_id_.find(pol_asso_id);
    if (it == by_id_.end())
        return false;
    if (it->second.pending_pti.has_value() && *it->second.pending_pti != pti)
        spdlog::info("pcf: PTI {:#04x} of {} replaced by the UE's request PTI {:#04x}; a later "
                     "answer for it is ignored (ADR-0563, single pending slot)",
                     *it->second.pending_pti,
                     pol_asso_id,
                     pti);
    it->second.pending_pti = pti;
    it->second.pending_requested = true;
    return true;
}

void DeliveryStore::set_subscriptions(const std::string& pol_asso_id,
                                      std::optional<std::string> udr,
                                      std::optional<std::string> amf) {
    std::lock_guard lock(mutex_);
    const auto it = by_id_.find(pol_asso_id);
    if (it == by_id_.end())
        return;
    if (udr.has_value())
        it->second.udr_subscription = std::move(*udr);
    if (amf.has_value())
        it->second.amf_subscription = std::move(*amf);
}

bool DeliveryStore::answer(const std::string& pol_asso_id, const UePolicyMessage& msg) {
    std::lock_guard lock(mutex_);
    const auto it = by_id_.find(pol_asso_id);
    if (it == by_id_.end() || !it->second.pending_pti.has_value() ||
        *it->second.pending_pti != msg.pti)
        return false;
    it->second.pending_pti.reset();
    it->second.pending_requested = false;
    it->second.last_result = msg.type;
    return true;
}

std::string udr_notify_path() {
    return std::string(kNotifyRoot) + "/udr-app-data";
}

std::string n1n2_failure_path(const std::string& pol_asso_id) {
    return std::string(kNotifyRoot) + "/n1n2-failure/" +
           sbi_core::uri::encode_path_segment(pol_asso_id);
}

std::string n1_notify_path(const std::string& pol_asso_id) {
    return std::string(kNotifyRoot) + "/n1-msg/" + sbi_core::uri::encode_path_segment(pol_asso_id);
}

tl::expected<std::vector<nlohmann::json>, std::string>
fetch_service_params(DeliveryDeps& deps, const std::string& supi) {
    auto token = deps.udr_oauth.get_bearer_token();
    if (!token.has_value())
        return tl::make_unexpected("no UDR token: " + token.error());
    sbi_core::http2::ClientRequest r;
    r.method = "GET";
    r.url = deps.udr_base_url + "/nudr-dr/v2/application-data/serviceParamData?supis=" +
            sbi_core::uri::encode_path_segment(supi);
    r.headers.emplace("authorization", "Bearer " + *token);
    const auto resp = deps.udr_client.send(r);
    if (!resp.has_value())
        return tl::make_unexpected("serviceParamData GET failed: " + resp.error());
    if (resp->status != 200)
        return tl::make_unexpected("serviceParamData GET answered " + std::to_string(resp->status));
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(resp->body);
    } catch (const std::exception& e) {
        return tl::make_unexpected(std::string("serviceParamData body is not JSON: ") + e.what());
    }
    if (!body.is_array())
        return tl::make_unexpected("serviceParamData body is not an array");
    std::vector<nlohmann::json> out;
    for (auto& entry : body) {
        // The supis filter is applied again here: an entry for another UE is never sent.
        if (entry.is_object() && entry.value("supi", std::string{}) == supi)
            out.push_back(std::move(entry));
    }
    return out;
}

void subscribe(DeliveryDeps& deps, DeliveryStore& store, const std::string& pol_asso_id) {
    const auto a = store.get(pol_asso_id);
    if (!a.has_value())
        return;
    if (a->udr_subscription.empty()) {
        if (auto token = deps.udr_oauth.get_bearer_token(); !token.has_value()) {
            spdlog::warn("pcf: no UDR token for subs-to-notify: {}", token.error());
        } else {
            nlohmann::json req = {
                {"notificationUri", deps.self_base_url + udr_notify_path()},
                {"dataFilters",
                 nlohmann::json::array(
                     {{{"dataInd", "SVC_PARAM"}, {"supis", nlohmann::json::array({a->supi})}}})}};
            sbi_core::http2::ClientRequest r;
            r.method = "POST";
            r.url = deps.udr_base_url + "/nudr-dr/v2/application-data/subs-to-notify";
            r.headers.emplace("content-type", "application/json");
            r.headers.emplace("authorization", "Bearer " + *token);
            r.body = req.dump();
            const auto resp = deps.udr_client.send(r);
            if (!resp.has_value()) {
                spdlog::warn("pcf: UDR subs-to-notify failed: {}", resp.error());
            } else if (resp->status != 201) {
                spdlog::warn("pcf: UDR subs-to-notify answered {}", resp->status);
            } else if (const auto loc = resp->headers.find("location");
                       loc == resp->headers.end() || loc->second.empty()) {
                spdlog::warn("pcf: UDR subs-to-notify 201 without a Location header");
            } else {
                store.set_subscriptions(pol_asso_id, loc->second, std::nullopt);
            }
        }
    }
    if (a->amf_subscription.empty()) {
        if (auto token = deps.amf_oauth.get_bearer_token(); !token.has_value()) {
            spdlog::warn("pcf: no AMF token for N1N2MessageSubscribe: {}", token.error());
        } else {
            nlohmann::json req = {
                {"n1MessageClass", "UPDP"},
                {"n1NotifyCallbackUri", deps.self_base_url + n1_notify_path(pol_asso_id)},
                {"nfId", deps.nf_instance_id}};
            sbi_core::http2::ClientRequest r;
            r.method = "POST";
            r.url = deps.amf_base_url + "/namf-comm/v1/ue-contexts/" +
                    sbi_core::uri::encode_path_segment(a->supi) + "/n1-n2-messages/subscriptions";
            r.headers.emplace("content-type", "application/json");
            r.headers.emplace("authorization", "Bearer " + *token);
            r.body = req.dump();
            const auto resp = deps.amf_client.send(r);
            if (!resp.has_value()) {
                spdlog::warn("pcf: N1N2MessageSubscribe failed: {}", resp.error());
            } else if (resp->status != 201) {
                spdlog::warn("pcf: N1N2MessageSubscribe answered {}", resp->status);
            } else {
                try {
                    const auto body = nlohmann::json::parse(resp->body);
                    const auto id = body.value("n1n2NotifySubscriptionId", std::string{});
                    if (id.empty())
                        spdlog::warn("pcf: N1N2MessageSubscribe 201 without an id");
                    else
                        store.set_subscriptions(pol_asso_id, std::nullopt, id);
                } catch (const std::exception& e) {
                    spdlog::warn("pcf: N1N2MessageSubscribe body is not JSON: {}", e.what());
                }
            }
        }
    }
}

void unsubscribe(DeliveryDeps& deps, const Association& a) {
    if (!a.udr_subscription.empty()) {
        if (auto token = deps.udr_oauth.get_bearer_token(); token.has_value()) {
            sbi_core::http2::ClientRequest r;
            r.method = "DELETE";
            // The Location is absolute (TS 29.500 §6.4.2 resource URI); a relative one is joined.
            r.url = a.udr_subscription.rfind("http", 0) == 0
                        ? a.udr_subscription
                        : deps.udr_base_url + a.udr_subscription;
            r.headers.emplace("authorization", "Bearer " + *token);
            const auto resp = deps.udr_client.send(r);
            if (!resp.has_value() || resp->status != 204)
                spdlog::warn("pcf: UDR unsubscribe {} did not answer 204", a.udr_subscription);
        } else {
            spdlog::warn("pcf: no UDR token to unsubscribe: {}", token.error());
        }
    }
    if (!a.amf_subscription.empty()) {
        if (auto token = deps.amf_oauth.get_bearer_token(); token.has_value()) {
            sbi_core::http2::ClientRequest r;
            r.method = "DELETE";
            r.url = deps.amf_base_url + "/namf-comm/v1/ue-contexts/" +
                    sbi_core::uri::encode_path_segment(a.supi) + "/n1-n2-messages/subscriptions/" +
                    sbi_core::uri::encode_path_segment(a.amf_subscription);
            r.headers.emplace("authorization", "Bearer " + *token);
            const auto resp = deps.amf_client.send(r);
            if (!resp.has_value() || resp->status != 204)
                spdlog::warn("pcf: N1N2MessageUnSubscribe {} did not answer 204",
                             a.amf_subscription);
        } else {
            spdlog::warn("pcf: no AMF token to unsubscribe: {}", token.error());
        }
    }
}

namespace {

enum class N1Send { kOk, kNotDelivered, kError };

// N1N2MessageTransfer of one UPDP message (TS 29.518 N1MessageContainer, class UPDP). kOk on
// 200 / 202; kNotDelivered on 409 / 504 (the UE could not be reached now); kError otherwise.
// With a pol_asso_id the request carries n1n2FailureTxfNotifURI so that the AMF reports a
// later delivery failure (P7). Failures are logged.
N1Send send_n1(DeliveryDeps& deps,
               const std::string& supi,
               const std::string& pol_asso_id,
               const Bytes& nas) {
    auto token = deps.amf_oauth.get_bearer_token();
    if (!token.has_value()) {
        spdlog::warn("pcf: no AMF token for N1N2MessageTransfer: {}", token.error());
        return N1Send::kError;
    }
    static constexpr const char* kContentId = "n1UePol";
    nlohmann::json req = {
        {"n1MessageContainer",
         {{"n1MessageClass", "UPDP"}, {"n1MessageContent", {{"contentId", kContentId}}}}}};
    if (!pol_asso_id.empty())
        req["n1n2FailureTxfNotifURI"] = deps.self_base_url + n1n2_failure_path(pol_asso_id);
    sbi_core::multipart::Part json_part;
    json_part.content_type = "application/json";
    json_part.body = req.dump();
    sbi_core::multipart::Part bin_part;
    bin_part.content_type = "application/vnd.3gpp.5gnas";
    bin_part.content_id = kContentId;
    bin_part.body.assign(nas.begin(), nas.end());
    const auto encoded = sbi_core::multipart::encode({json_part, bin_part});

    sbi_core::http2::ClientRequest r;
    r.method = "POST";
    r.url = deps.amf_base_url + "/namf-comm/v1/ue-contexts/" +
            sbi_core::uri::encode_path_segment(supi) + "/n1-n2-messages";
    r.headers.emplace("content-type", encoded.content_type_header);
    r.headers.emplace("authorization", "Bearer " + *token);
    r.body = encoded.body;
    const auto resp = deps.amf_client.send(r);
    if (!resp.has_value()) {
        spdlog::warn("pcf: UPDP N1N2MessageTransfer for {} failed: {}", supi, resp.error());
        return N1Send::kError;
    }
    if (resp->status == 200 || resp->status == 202)
        return N1Send::kOk;
    spdlog::warn("pcf: UPDP N1N2MessageTransfer for {} answered {}", supi, resp->status);
    if (resp->status == 409 || resp->status == 504)
        return N1Send::kNotDelivered;
    return N1Send::kError;
}

DeliveryResult failed_send(DeliveryStore& store, const std::string& pol_asso_id, N1Send sent) {
    store.release_pti(pol_asso_id);
    if (sent == N1Send::kNotDelivered) {
        store.set_delivery_failure(pol_asso_id, true);
        return DeliveryResult::kNotDelivered;
    }
    return DeliveryResult::kFailed;
}

} // namespace

DeliveryResult deliver(DeliveryDeps& deps, DeliveryStore& store, const std::string& pol_asso_id) {
    const auto a = store.get(pol_asso_id);
    if (!a.has_value())
        return DeliveryResult::kNothingToSend;
    if (!a->v2x && !a->a2x)
        return DeliveryResult::kNothingToSend;
    const auto params = fetch_service_params(deps, a->supi);
    if (!params.has_value()) {
        spdlog::warn("pcf: V2X/A2X service parameters for {}: {}", a->supi, params.error());
        return DeliveryResult::kFailed;
    }
    const auto parts = parts_from_service_params(*params, a->v2x, a->a2x);
    if (parts.empty()) {
        spdlog::info("pcf: no V2XP/A2XP service parameters for {}; no UE policy sent", a->supi);
        return DeliveryResult::kNothingToSend;
    }
    const auto pti = store.allocate_pti(pol_asso_id);
    if (!pti.has_value()) {
        spdlog::warn("pcf: no free PTI for {}", a->supi);
        return DeliveryResult::kFailed;
    }
    const auto command = build_manage_ue_policy_command(*pti, a->plmn, kV2xSectionCode, parts);
    if (!command.has_value()) {
        spdlog::error("pcf: MANAGE UE POLICY COMMAND not built: {}", command.error());
        store.release_pti(pol_asso_id);
        return DeliveryResult::kFailed;
    }
    if (const auto sent = send_n1(deps, a->supi, pol_asso_id, *command); sent != N1Send::kOk)
        return failed_send(store, pol_asso_id, sent);
    spdlog::info("pcf: MANAGE UE POLICY COMMAND (PTI {:#04x}, {} part(s)) sent for {}",
                 *pti,
                 parts.size(),
                 a->supi);
    return DeliveryResult::kSent;
}

DeliveryResult send_provisioning_reject(DeliveryDeps& deps,
                                        const std::string& supi,
                                        std::uint8_t pti,
                                        std::uint8_t cause) {
    if (send_n1(deps, supi, {}, build_provisioning_reject(pti, cause)) != N1Send::kOk) {
        // TS 24.587 §5.3.2.5: the procedure is aborted; nothing is kept.
        spdlog::warn("pcf: UE POLICY PROVISIONING REJECT (PTI {:#04x}, #{}) for {} not "
                     "transferred; procedure aborted",
                     pti,
                     cause,
                     supi);
        return DeliveryResult::kFailed;
    }
    spdlog::info(
        "pcf: UE POLICY PROVISIONING REJECT (PTI {:#04x}, #{}) sent for {}", pti, cause, supi);
    return DeliveryResult::kRejected;
}

DeliveryResult answer_provisioning_request(DeliveryDeps& deps,
                                           DeliveryStore& store,
                                           const std::string& pol_asso_id,
                                           const ProvisioningRequest& req) {
    const auto a = store.get(pol_asso_id);
    if (!a.has_value())
        return DeliveryResult::kNothingToSend;
    // §5.3.2.4 #35: the PTI is pending on a network-initiated COMMAND. A PTI pending from the
    // UE's own earlier request is its T5040 retransmission (§5.3.2.6): answered again below.
    if (store.pti_in_use(pol_asso_id, req.pti))
        return send_provisioning_reject(deps, a->supi, req.pti, kUpdsPtiAlreadyInUse);
    const PolicySelection sel{req.v2x_pc5 && a->v2x, req.v2x_uu && a->v2x, req.a2x && a->a2x};
    if (!sel.v2x_pc5 && !sel.v2x_uu && !sel.a2x) {
        spdlog::info("pcf: UE policy request of {} asks for nothing it is authorized for", a->supi);
        return send_provisioning_reject(deps, a->supi, req.pti, kUpdsServiceOptionNotSupported);
    }
    const auto params = fetch_service_params(deps, a->supi);
    if (!params.has_value()) {
        spdlog::warn("pcf: V2X/A2X service parameters for {}: {}", a->supi, params.error());
        return send_provisioning_reject(
            deps, a->supi, req.pti, kUpdsServiceOptionTemporarilyOutOfOrder);
    }
    const auto parts = parts_from_service_params(*params, sel);
    if (parts.empty()) {
        spdlog::info("pcf: no requested V2XP/A2XP service parameters for {}", a->supi);
        return send_provisioning_reject(deps, a->supi, req.pti, kUpdsRequestRejectedUnspecified);
    }
    const auto command = build_manage_ue_policy_command(req.pti, a->plmn, kV2xSectionCode, parts);
    if (!command.has_value()) {
        spdlog::error("pcf: MANAGE UE POLICY COMMAND not built: {}", command.error());
        return send_provisioning_reject(deps, a->supi, req.pti, kUpdsRequestRejectedUnspecified);
    }
    if (!store.claim_requested_pti(pol_asso_id, req.pti))
        return DeliveryResult::kNothingToSend;
    if (const auto sent = send_n1(deps, a->supi, pol_asso_id, *command); sent != N1Send::kOk)
        return failed_send(store, pol_asso_id, sent);
    spdlog::info("pcf: MANAGE UE POLICY COMMAND (requested, PTI {:#04x}, {} part(s)) sent for {}",
                 req.pti,
                 parts.size(),
                 a->supi);
    return DeliveryResult::kSent;
}

} // namespace pcf::updp
