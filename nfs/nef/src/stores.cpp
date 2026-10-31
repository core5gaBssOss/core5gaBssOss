#include "stores.hpp"

namespace nef {

void PfdCatalogStore::seed(const std::string& application_id, nlohmann::json pfd_data_for_app) {
    std::lock_guard<std::mutex> lock(mutex_);
    catalog_[application_id] = std::move(pfd_data_for_app);
}

std::optional<nlohmann::json> PfdCatalogStore::get(const std::string& application_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = catalog_.find(application_id);
    if (it == catalog_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

std::vector<nlohmann::json>
PfdCatalogStore::get_many(const std::vector<std::string>& application_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<nlohmann::json> result;
    for (const auto& id : application_ids) {
        auto it = catalog_.find(id);
        if (it != catalog_.end()) {
            result.push_back(it->second);
        }
    }
    return result;
}

std::optional<nlohmann::json>
PfdCatalogStore::get_if_changed_since(const std::string& application_id,
                                      const std::optional<std::string>& since) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = catalog_.find(application_id);
    if (it == catalog_.end()) {
        return std::nullopt;
    }
    if (!since.has_value()) {
        return std::make_optional(it->second);
    }
    const std::string stored_timestamp = it->second.value("pfdTimestamp", "");
    if (stored_timestamp > *since) {
        return std::make_optional(it->second);
    }
    return std::nullopt;
}

std::expected<std::string, sbi_core::Refusal>
PfdSubscriptionStore::create(nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "pfdsub-" + std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(id, consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> PfdSubscriptionStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool PfdSubscriptionStore::put(const std::string& sub_id, nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

bool PfdSubscriptionStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(sub_id);
    return subscriptions_.erase(sub_id) > 0;
}

std::expected<std::string, sbi_core::Refusal> DnaiMapSubStore::create(nlohmann::json subscription,
                                                                      const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "dnaimapsub-" + std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(id, consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> DnaiMapSubStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool DnaiMapSubStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(sub_id);
    return subscriptions_.erase(sub_id) > 0;
}

std::expected<std::string, sbi_core::Refusal>
EasDeploySubStore::create(nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "easdeploysub-" + std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(id, consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> EasDeploySubStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool EasDeploySubStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(sub_id);
    return subscriptions_.erase(sub_id) > 0;
}

std::expected<std::string, sbi_core::Refusal> SmContextStore::create(nlohmann::json created_data,
                                                                     const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "smctx-" + std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(id, consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    contexts_.emplace(id, std::move(created_data));
    return id;
}

std::optional<nlohmann::json> SmContextStore::get(const std::string& sm_context_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(sm_context_id);
    if (it == contexts_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool SmContextStore::update(const std::string& sm_context_id,
                            const nlohmann::json& partial_update) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = contexts_.find(sm_context_id);
    if (it == contexts_.end()) {
        return false;
    }
    for (const auto& [key, value] : partial_update.items()) {
        it->second[key] = value;
    }
    return true;
}

bool SmContextStore::remove(const std::string& sm_context_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(sm_context_id);
    return contexts_.erase(sm_context_id) > 0;
}

std::expected<std::string, sbi_core::Refusal>
EcsAddrCfgInfoSubStore::create(nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "ecsaddrsub-" + std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(id, consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> EcsAddrCfgInfoSubStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool EcsAddrCfgInfoSubStore::put(const std::string& sub_id, nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

bool EcsAddrCfgInfoSubStore::patch(const std::string& sub_id, const nlohmann::json& merge_patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second.merge_patch(merge_patch);
    return true;
}

bool EcsAddrCfgInfoSubStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(sub_id);
    return subscriptions_.erase(sub_id) > 0;
}

std::expected<std::string, sbi_core::Refusal>
NefEventExposureSubStore::create(nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "evtexpsub-" + std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(id, consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> NefEventExposureSubStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool NefEventExposureSubStore::put(const std::string& sub_id, nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

bool NefEventExposureSubStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(sub_id);
    return subscriptions_.erase(sub_id) > 0;
}

std::size_t
NefEventExposureSubStore::sweep(const std::function<SweepAction(nlohmann::json&)>& decide) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t removed = 0;
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
        if (decide(it->second) == SweepAction::remove) {
            quota_.release(it->first);
            it = subscriptions_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

std::expected<std::string, sbi_core::Refusal>
TrafficInfluDataSubStore::create(nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "trafficinfsub-" + std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(id, consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> TrafficInfluDataSubStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool TrafficInfluDataSubStore::put(const std::string& sub_id, nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

bool TrafficInfluDataSubStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(sub_id);
    return subscriptions_.erase(sub_id) > 0;
}

std::expected<std::string, sbi_core::Refusal>
InferEventSubStore::create(nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "infersub-" + std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(id, consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> InferEventSubStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool InferEventSubStore::put(const std::string& sub_id, nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

bool InferEventSubStore::patch(const std::string& sub_id, const nlohmann::json& merge_patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second.merge_patch(merge_patch);
    return true;
}

bool InferEventSubStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(sub_id);
    return subscriptions_.erase(sub_id) > 0;
}

std::expected<std::string, sbi_core::Refusal>
TrainEventsSubStore::create(nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "trainsub-" + std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(id, consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> TrainEventsSubStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool TrainEventsSubStore::put(const std::string& sub_id, nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

bool TrainEventsSubStore::patch(const std::string& sub_id, const nlohmann::json& merge_patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second.merge_patch(merge_patch);
    return true;
}

bool TrainEventsSubStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(sub_id);
    return subscriptions_.erase(sub_id) > 0;
}

std::expected<std::string, sbi_core::Refusal>
VflInferSubStore::create(nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "vflinfersub-" + std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(id, consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> VflInferSubStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool VflInferSubStore::put(const std::string& sub_id, nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

bool VflInferSubStore::patch(const std::string& sub_id, const nlohmann::json& merge_patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second.merge_patch(merge_patch);
    return true;
}

bool VflInferSubStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(sub_id);
    return subscriptions_.erase(sub_id) > 0;
}

std::expected<std::string, sbi_core::Refusal>
NefVflTrainSubStore::create(nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string id = "vfltrainsub-" + std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(id, consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_.emplace(id, std::move(subscription));
    return id;
}

std::optional<nlohmann::json> NefVflTrainSubStore::get(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return std::make_optional(it->second);
}

bool NefVflTrainSubStore::put(const std::string& sub_id, nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

bool NefVflTrainSubStore::patch(const std::string& sub_id, const nlohmann::json& merge_patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(sub_id);
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second.merge_patch(merge_patch);
    return true;
}

bool NefVflTrainSubStore::remove(const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(sub_id);
    return subscriptions_.erase(sub_id) > 0;
}

// --- ADR-0302: AF-facing TrafficInfluence subscriptions ---

std::expected<std::string, sbi_core::Refusal> AfTrafficInfluenceSubStore::create(
    const std::string& af_id, nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto sub_id = std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(key(af_id, sub_id), consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_[key(af_id, sub_id)] = std::move(subscription);
    return sub_id;
}

std::optional<nlohmann::json> AfTrafficInfluenceSubStore::get(const std::string& af_id,
                                                              const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(key(af_id, sub_id));
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<nlohmann::json> AfTrafficInfluenceSubStore::list(const std::string& af_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto prefix = af_id + "/";
    std::vector<nlohmann::json> out;
    for (const auto& [k, v] : subscriptions_) {
        if (k.rfind(prefix, 0) == 0) {
            out.push_back(v);
        }
    }
    return out;
}

bool AfTrafficInfluenceSubStore::put(const std::string& af_id,
                                     const std::string& sub_id,
                                     nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(key(af_id, sub_id));
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

std::optional<nlohmann::json> AfTrafficInfluenceSubStore::merge_patch(const std::string& af_id,
                                                                      const std::string& sub_id,
                                                                      const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(key(af_id, sub_id));
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return it->second;
}

bool AfTrafficInfluenceSubStore::remove(const std::string& af_id, const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(key(af_id, sub_id));
    return subscriptions_.erase(key(af_id, sub_id)) > 0;
}

// --- ADR-0315: AF-facing AsSessionWithQoS subscriptions ---

std::expected<std::string, sbi_core::Refusal> AfQosSubStore::create(const std::string& af_id,
                                                                    nlohmann::json subscription,
                                                                    const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto sub_id = std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(key(af_id, sub_id), consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_[key(af_id, sub_id)] = std::move(subscription);
    return sub_id;
}

std::optional<nlohmann::json> AfQosSubStore::get(const std::string& af_id,
                                                 const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(key(af_id, sub_id));
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<nlohmann::json> AfQosSubStore::list(const std::string& af_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto prefix = af_id + "/";
    std::vector<nlohmann::json> out;
    for (const auto& [k, v] : subscriptions_) {
        if (k.rfind(prefix, 0) == 0) {
            out.push_back(v);
        }
    }
    return out;
}

bool AfQosSubStore::put(const std::string& af_id,
                        const std::string& sub_id,
                        nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(key(af_id, sub_id));
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

std::optional<nlohmann::json> AfQosSubStore::merge_patch(const std::string& af_id,
                                                         const std::string& sub_id,
                                                         const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(key(af_id, sub_id));
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return it->second;
}

bool AfQosSubStore::remove(const std::string& af_id, const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    app_sessions_.erase(key(af_id, sub_id));
    quota_.release(key(af_id, sub_id));
    return subscriptions_.erase(key(af_id, sub_id)) > 0;
}

void AfQosSubStore::set_app_session(const std::string& af_id,
                                    const std::string& sub_id,
                                    const std::string& app_session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    app_sessions_[key(af_id, sub_id)] = app_session_id;
}

std::optional<std::string> AfQosSubStore::app_session(const std::string& af_id,
                                                      const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = app_sessions_.find(key(af_id, sub_id));
    if (it == app_sessions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

// --- ADR-0316: AF-facing MonitoringEvent subscriptions ---

std::expected<std::string, sbi_core::Refusal> AfMonitoringSubStore::create(
    const std::string& af_id, nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto sub_id = std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(key(af_id, sub_id), consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_[key(af_id, sub_id)] = std::move(subscription);
    return sub_id;
}

std::optional<nlohmann::json> AfMonitoringSubStore::get(const std::string& af_id,
                                                        const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(key(af_id, sub_id));
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<nlohmann::json> AfMonitoringSubStore::list(const std::string& af_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto prefix = af_id + "/";
    std::vector<nlohmann::json> out;
    for (const auto& [k, v] : subscriptions_) {
        if (k.rfind(prefix, 0) == 0) {
            out.push_back(v);
        }
    }
    return out;
}

bool AfMonitoringSubStore::put(const std::string& af_id,
                               const std::string& sub_id,
                               nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(key(af_id, sub_id));
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

bool AfMonitoringSubStore::remove(const std::string& af_id, const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    ee_subs_.erase(key(af_id, sub_id));
    quota_.release(key(af_id, sub_id));
    return subscriptions_.erase(key(af_id, sub_id)) > 0;
}

std::vector<std::pair<std::string, std::string>>
AfMonitoringSubStore::sweep(const std::function<SweepAction(nlohmann::json&)>& decide) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, std::string>> ee_removed;
    for (auto it = subscriptions_.begin(); it != subscriptions_.end();) {
        if (decide(it->second) != SweepAction::remove) {
            ++it;
            continue;
        }
        if (const auto ee = ee_subs_.find(it->first); ee != ee_subs_.end()) {
            ee_removed.push_back(ee->second);
            ee_subs_.erase(ee);
        }
        quota_.release(it->first);
        it = subscriptions_.erase(it);
    }
    return ee_removed;
}

void AfMonitoringSubStore::set_ee_subscription(const std::string& af_id,
                                               const std::string& sub_id,
                                               const std::string& ue_identity,
                                               const std::string& ee_sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    ee_subs_[key(af_id, sub_id)] = {ue_identity, ee_sub_id};
}

std::optional<std::pair<std::string, std::string>>
AfMonitoringSubStore::ee_subscription(const std::string& af_id, const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = ee_subs_.find(key(af_id, sub_id));
    if (it == ee_subs_.end()) {
        return std::nullopt;
    }
    return it->second;
}

// --- ADR-0319: AF-provisioned UE-ID mappings ---

std::expected<std::string, sbi_core::Refusal> AfUeIdMappingStore::create(
    const std::string& af_id, nlohmann::json mapping, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto id = std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(key(af_id, id), consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    mappings_[key(af_id, id)] = std::move(mapping);
    return id;
}

std::optional<nlohmann::json> AfUeIdMappingStore::get(const std::string& af_id,
                                                      const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mappings_.find(key(af_id, id));
    if (it == mappings_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<nlohmann::json> AfUeIdMappingStore::list(const std::string& af_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto prefix = af_id + "/";
    std::vector<nlohmann::json> out;
    for (const auto& [k, v] : mappings_) {
        if (k.rfind(prefix, 0) == 0) {
            out.push_back(v);
        }
    }
    return out;
}

bool AfUeIdMappingStore::put(const std::string& af_id,
                             const std::string& id,
                             nlohmann::json mapping) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mappings_.find(key(af_id, id));
    if (it == mappings_.end()) {
        return false;
    }
    it->second = std::move(mapping);
    return true;
}

std::optional<nlohmann::json> AfUeIdMappingStore::merge_patch(const std::string& af_id,
                                                              const std::string& id,
                                                              const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = mappings_.find(key(af_id, id));
    if (it == mappings_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return it->second;
}

bool AfUeIdMappingStore::remove(const std::string& af_id, const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(key(af_id, id));
    return mappings_.erase(key(af_id, id)) > 0;
}

// --- ADR-0321: AF-facing ServiceParameter subscriptions ---

std::expected<std::string, sbi_core::Refusal> AfServiceParamSubStore::create(
    const std::string& af_id, nlohmann::json subscription, const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto sub_id = std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(key(af_id, sub_id), consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    subscriptions_[key(af_id, sub_id)] = std::move(subscription);
    return sub_id;
}

std::optional<nlohmann::json> AfServiceParamSubStore::get(const std::string& af_id,
                                                          const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(key(af_id, sub_id));
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<nlohmann::json> AfServiceParamSubStore::list(const std::string& af_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto prefix = af_id + "/";
    std::vector<nlohmann::json> out;
    for (const auto& [k, v] : subscriptions_) {
        if (k.rfind(prefix, 0) == 0) {
            out.push_back(v);
        }
    }
    return out;
}

bool AfServiceParamSubStore::put(const std::string& af_id,
                                 const std::string& sub_id,
                                 nlohmann::json subscription) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(key(af_id, sub_id));
    if (it == subscriptions_.end()) {
        return false;
    }
    it->second = std::move(subscription);
    return true;
}

std::optional<nlohmann::json> AfServiceParamSubStore::merge_patch(const std::string& af_id,
                                                                  const std::string& sub_id,
                                                                  const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = subscriptions_.find(key(af_id, sub_id));
    if (it == subscriptions_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return it->second;
}

bool AfServiceParamSubStore::remove(const std::string& af_id, const std::string& sub_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(key(af_id, sub_id));
    return subscriptions_.erase(key(af_id, sub_id)) > 0;
}

// --- ADR-0322: shared per-AF document store ---

std::expected<std::string, sbi_core::Refusal> AfDocumentStore::create(const std::string& af_id,
                                                                      nlohmann::json document,
                                                                      const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto id = std::to_string(next_id_++);
    if (const auto refused = quota_.try_admit(key(af_id, id), consumer)) {
        --next_id_;
        return std::unexpected(*refused);
    }
    documents_[key(af_id, id)] = std::move(document);
    return id;
}

std::optional<nlohmann::json> AfDocumentStore::get(const std::string& af_id,
                                                   const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = documents_.find(key(af_id, id));
    if (it == documents_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<nlohmann::json> AfDocumentStore::list(const std::string& af_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto prefix = af_id + "/";
    std::vector<nlohmann::json> out;
    for (const auto& [k, v] : documents_) {
        if (k.rfind(prefix, 0) == 0) {
            out.push_back(v);
        }
    }
    return out;
}

bool AfDocumentStore::put(const std::string& af_id,
                          const std::string& id,
                          nlohmann::json document) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = documents_.find(key(af_id, id));
    if (it == documents_.end()) {
        return false;
    }
    it->second = std::move(document);
    return true;
}

std::optional<nlohmann::json> AfDocumentStore::merge_patch(const std::string& af_id,
                                                           const std::string& id,
                                                           const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = documents_.find(key(af_id, id));
    if (it == documents_.end()) {
        return std::nullopt;
    }
    it->second.merge_patch(patch);
    return it->second;
}

std::expected<bool, sbi_core::Refusal>
AfDocumentStore::create_with_id(const std::string& af_id,
                                const std::string& id,
                                nlohmann::json document,
                                const std::string& consumer) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto refused = quota_.try_admit(key(af_id, id), consumer)) {
        return std::unexpected(*refused);
    }
    return documents_.emplace(key(af_id, id), std::move(document)).second;
}

std::optional<nlohmann::json> AfDocumentStore::json_patch(const std::string& af_id,
                                                          const std::string& id,
                                                          const nlohmann::json& patch) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = documents_.find(key(af_id, id));
    if (it == documents_.end()) {
        return std::nullopt;
    }
    // nlohmann::json::patch is all-or-nothing -- it throws rather than leaving a half-applied
    // document -- so assigning its result only on success keeps the stored document intact when a
    // patch is rejected. The exception is deliberately not caught here: the handler needs its
    // message for the ProblemDetails body.
    it->second = it->second.patch(patch);
    return it->second;
}

bool AfDocumentStore::remove(const std::string& af_id, const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    quota_.release(key(af_id, id));
    return documents_.erase(key(af_id, id)) > 0;
}

AfDocumentStore::SweepCount
AfDocumentStore::sweep(const std::function<SweepAction(nlohmann::json&)>& decide) {
    std::lock_guard<std::mutex> lock(mutex_);
    SweepCount count;
    for (auto it = documents_.begin(); it != documents_.end();) {
        const auto action = decide(it->second);
        if (action == SweepAction::remove) {
            quota_.release(it->first);
            it = documents_.erase(it);
            ++count.removed;
            continue;
        }
        if (action == SweepAction::rewrite) {
            ++count.rewritten;
        }
        ++it;
    }
    return count;
}

} // namespace nef
