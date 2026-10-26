#include "stores.hpp"

namespace udm {
namespace {

// The quota key of a (ue_id, sub-id) pair in the two nested registration stores.
std::string pair_key(const std::string& a, const std::string& b) {
    return a + '\x1f' + b;
}

} // namespace

bool AmfRegistrationStore::put(const std::string& ue_id,
                               nlohmann::json registration,
                               const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!quota_.admit(ue_id, consumer)) {
        return false;
    }
    registrations_[ue_id] = std::move(registration);
    return true;
}

std::optional<nlohmann::json> AmfRegistrationStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> AmfRegistrationStore::merge_patch(const std::string& ue_id,
                                                                const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return std::make_optional(it->second);
}

bool AmfRegistrationStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(ue_id);
    return registrations_.erase(ue_id) > 0;
}

bool AmfNon3GppRegistrationStore::put(const std::string& ue_id,
                                      nlohmann::json registration,
                                      const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!quota_.admit(ue_id, consumer)) {
        return false;
    }
    registrations_[ue_id] = std::move(registration);
    return true;
}

std::optional<nlohmann::json> AmfNon3GppRegistrationStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json>
AmfNon3GppRegistrationStore::merge_patch(const std::string& ue_id, const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return std::make_optional(it->second);
}

bool SmsfRegistrationStore::put(const std::string& ue_id,
                                nlohmann::json registration,
                                const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!quota_.admit(ue_id, consumer)) {
        return false;
    }
    registrations_[ue_id] = std::move(registration);
    return true;
}

std::optional<nlohmann::json> SmsfRegistrationStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> SmsfRegistrationStore::merge_patch(const std::string& ue_id,
                                                                 const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return std::make_optional(it->second);
}

bool SmsfRegistrationStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(ue_id);
    return registrations_.erase(ue_id) > 0;
}

bool IpSmGwRegistrationStore::put(const std::string& ue_id,
                                  nlohmann::json registration,
                                  const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!quota_.admit(ue_id, consumer)) {
        return false;
    }
    registrations_[ue_id] = std::move(registration);
    return true;
}

std::optional<nlohmann::json> IpSmGwRegistrationStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = registrations_.find(ue_id);
    if (it == registrations_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool IpSmGwRegistrationStore::remove(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(ue_id);
    return registrations_.erase(ue_id) > 0;
}

bool RoamingInfoUpdateStore::put(const std::string& ue_id,
                                 nlohmann::json info,
                                 const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!quota_.admit(ue_id, consumer)) {
        return false;
    }
    info_[ue_id] = std::move(info);
    return true;
}

std::optional<nlohmann::json> RoamingInfoUpdateStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = info_.find(ue_id);
    if (it == info_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool SmfRegistrationStore::put(const std::string& ue_id,
                               const std::string& pdu_session_id,
                               nlohmann::json registration,
                               const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!quota_.admit(pair_key(ue_id, pdu_session_id), consumer)) {
        return false;
    }
    registrations_[ue_id][pdu_session_id] = std::move(registration);
    return true;
}

std::optional<nlohmann::json> SmfRegistrationStore::get(const std::string& ue_id,
                                                        const std::string& pdu_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return std::nullopt;
    }
    auto session_it = ue_it->second.find(pdu_session_id);
    if (session_it == ue_it->second.end()) {
        return std::nullopt;
    }
    return std::make_optional(session_it->second);
}

std::optional<nlohmann::json> SmfRegistrationStore::merge_patch(const std::string& ue_id,
                                                                const std::string& pdu_session_id,
                                                                const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return std::nullopt;
    }
    auto session_it = ue_it->second.find(pdu_session_id);
    if (session_it == ue_it->second.end()) {
        return std::nullopt;
    }
    session_it->second.merge_patch(patch);
    return std::make_optional(session_it->second);
}

bool SmfRegistrationStore::remove(const std::string& ue_id, const std::string& pdu_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return false;
    }
    quota_.release(pair_key(ue_id, pdu_session_id));
    const bool removed = ue_it->second.erase(pdu_session_id) > 0;
    if (ue_it->second.empty()) {
        registrations_.erase(ue_it);
    }
    return removed;
}

std::vector<nlohmann::json> SmfRegistrationStore::list_for_ue(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> result;
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return result;
    }
    result.reserve(ue_it->second.size());
    for (const auto& [pdu_session_id, registration] : ue_it->second) {
        result.push_back(registration);
    }
    return result;
}

bool NwdafRegistrationStore::put(const std::string& ue_id,
                                 const std::string& nwdaf_registration_id,
                                 nlohmann::json registration,
                                 const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!quota_.admit(pair_key(ue_id, nwdaf_registration_id), consumer)) {
        return false;
    }
    registrations_[ue_id][nwdaf_registration_id] = std::move(registration);
    return true;
}

std::optional<nlohmann::json>
NwdafRegistrationStore::get(const std::string& ue_id, const std::string& nwdaf_registration_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return std::nullopt;
    }
    auto reg_it = ue_it->second.find(nwdaf_registration_id);
    if (reg_it == ue_it->second.end()) {
        return std::nullopt;
    }
    return std::make_optional(reg_it->second);
}

std::optional<nlohmann::json>
NwdafRegistrationStore::merge_patch(const std::string& ue_id,
                                    const std::string& nwdaf_registration_id,
                                    const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return std::nullopt;
    }
    auto reg_it = ue_it->second.find(nwdaf_registration_id);
    if (reg_it == ue_it->second.end()) {
        return std::nullopt;
    }
    reg_it->second.merge_patch(patch);
    return std::make_optional(reg_it->second);
}

bool NwdafRegistrationStore::remove(const std::string& ue_id,
                                    const std::string& nwdaf_registration_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return false;
    }
    quota_.release(pair_key(ue_id, nwdaf_registration_id));
    const bool removed = ue_it->second.erase(nwdaf_registration_id) > 0;
    if (ue_it->second.empty()) {
        registrations_.erase(ue_it);
    }
    return removed;
}

std::vector<nlohmann::json> NwdafRegistrationStore::list_for_ue(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> result;
    auto ue_it = registrations_.find(ue_id);
    if (ue_it == registrations_.end()) {
        return result;
    }
    result.reserve(ue_it->second.size());
    for (const auto& [nwdaf_registration_id, registration] : ue_it->second) {
        result.push_back(registration);
    }
    return result;
}

std::optional<std::string> SdmSubscriptionStore::create(SdmSubscriptionEntry entry,
                                                        const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "sdmsub-" + std::to_string(next_id_);
    if (!quota_.admit(id, consumer)) {
        return std::nullopt;
    }
    ++next_id_;
    subscriptions_.emplace(id, std::move(entry));
    return id;
}

std::optional<SdmSubscriptionEntry> SdmSubscriptionStore::get(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool SdmSubscriptionStore::remove(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(subscription_id);
    return subscriptions_.erase(subscription_id) > 0;
}

std::optional<SdmSubscriptionEntry>
SdmSubscriptionStore::merge_patch(const std::string& subscription_id,
                                  const std::string& ue_id,
                                  const nlohmann::json& patch,
                                  const ExpiryFinisher& finish) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end() || it->second.ue_id != ue_id) {
        return std::nullopt;
    }
    nlohmann::json data = it->second.data;
    data.merge_patch(patch);
    const auto expires_at = finish(data); // may throw: nothing changed yet
    it->second.data = std::move(data);
    it->second.expires_at = expires_at;
    return std::make_optional(it->second);
}

std::size_t SdmSubscriptionStore::remove_expired(Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::erase_if(subscriptions_, [&](const auto& kv) {
        if (kv.second.expires_at > now) {
            return false;
        }
        quota_.release(kv.first);
        return true;
    });
}

std::optional<std::string> SharedDataSubscriptionStore::create(nlohmann::json data,
                                                               Clock::time_point expires_at,
                                                               const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "shdsub-" + std::to_string(next_id_);
    if (!quota_.admit(id, consumer)) {
        return std::nullopt;
    }
    ++next_id_;
    subscriptions_.emplace(id, Entry{.data = std::move(data), .expires_at = expires_at});
    return id;
}

std::optional<nlohmann::json> SharedDataSubscriptionStore::get(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second.data);
}

bool SharedDataSubscriptionStore::remove(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(subscription_id);
    return subscriptions_.erase(subscription_id) > 0;
}

std::optional<nlohmann::json> SharedDataSubscriptionStore::merge_patch(
    const std::string& subscription_id, const nlohmann::json& patch, const ExpiryFinisher& finish) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    nlohmann::json data = it->second.data;
    data.merge_patch(patch);
    const auto expires_at = finish(data); // may throw: nothing changed yet
    it->second.data = std::move(data);
    it->second.expires_at = expires_at;
    return std::make_optional(it->second.data);
}

std::size_t SharedDataSubscriptionStore::remove_expired(Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::erase_if(subscriptions_, [&](const auto& kv) {
        if (kv.second.expires_at > now) {
            return false;
        }
        quota_.release(kv.first);
        return true;
    });
}

std::optional<std::string>
AuthEventStore::create(const std::string& supi, nlohmann::json event, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "authevent-" + std::to_string(next_id_);
    if (!quota_.admit(id, consumer)) {
        return std::nullopt;
    }
    ++next_id_;
    events_.emplace(id, Entry{.supi = supi, .event = std::move(event)});
    return id;
}

bool AuthEventStore::remove(const std::string& supi, const std::string& auth_event_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = events_.find(auth_event_id);
    if (it == events_.end() || it->second.supi != supi) {
        return false;
    }
    quota_.release(auth_event_id);
    events_.erase(it);
    return true;
}

bool AuthEventStore::has_successful_event(const std::string& supi) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, entry] : events_) {
        if (entry.supi == supi && entry.event.value("success", false)) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> EeSubscriptionStore::create(EeSubscriptionEntry entry,
                                                       const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "eesub-" + std::to_string(next_id_);
    if (!quota_.admit(id, consumer)) {
        return std::nullopt;
    }
    ++next_id_;
    subscriptions_.emplace(id, std::move(entry));
    return id;
}

std::optional<EeSubscriptionEntry> EeSubscriptionStore::get(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> EeSubscriptionStore::apply_patch(const std::string& subscription_id,
                                                               const nlohmann::json& patch_ops,
                                                               const ExpiryFinisher& finish) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(subscription_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    nlohmann::json data = it->second.data.patch(patch_ops);
    const auto expires_at = finish(data); // may throw: nothing changed yet
    it->second.data = std::move(data);
    it->second.expires_at = expires_at;
    return std::make_optional(it->second.data);
}

bool EeSubscriptionStore::remove(const std::string& subscription_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(subscription_id);
    return subscriptions_.erase(subscription_id) > 0;
}

std::size_t EeSubscriptionStore::remove_expired(Clock::time_point now) {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::erase_if(subscriptions_, [&](const auto& kv) {
        if (kv.second.expires_at > now) {
            return false;
        }
        quota_.release(kv.first);
        return true;
    });
}

std::optional<nlohmann::json> PpDataStore::get(const std::string& ue_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pp_data_.find(ue_id);
    if (it == pp_data_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::optional<nlohmann::json> PpDataStore::merge_patch(const std::string& ue_id,
                                                       const nlohmann::json& patch,
                                                       const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = pp_data_.find(ue_id);
    if (it == pp_data_.end()) {
        if (!quota_.admit(ue_id, consumer)) {
            return std::nullopt;
        }
        // Real spec behavior: PpData is nullable and the resource is a singular per-UE document
        // that may not exist yet -- a merge-patch onto "no document" starts from an empty object,
        // same real RFC 7396 semantics as PATCHing a not-yet-existing resource elsewhere in this
        // project (e.g. AMF's own N1N2 subscription creation-on-demand shape).
        nlohmann::json doc = nlohmann::json::object();
        doc.merge_patch(patch);
        pp_data_[ue_id] = doc;
        return std::make_optional(doc);
    }
    it->second.merge_patch(patch);
    return std::make_optional(it->second);
}

} // namespace udm
