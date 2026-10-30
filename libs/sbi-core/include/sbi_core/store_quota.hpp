#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>

// REVIEW-5ae0ef1 SUS-02 (ADR-0535): bounds on an NF's in-memory resource stores, so that no client
// can grow one without limit. The limits are operator configuration (CLAUDE.md guardrail 8); a
// create refused by them is answered 500 INSUFFICIENT_RESOURCES (TS 29.500 Table 5.2.7.2-1) unless
// the API defines its own cause.
namespace sbi_core {

struct StoreLimits {
    std::size_t max_entries = 0;      // per store
    std::size_t max_per_consumer = 0; // per store, per access-token sub (http2::Request::token_sub)
};

// Who created each key of one store, and the counts the limits apply to. Not synchronised: the
// owning store calls it under its own lock. A consumer "" (a request with no token) counts only
// toward max_entries.
class StoreQuota {
public:
    explicit StoreQuota(StoreLimits limits) : limits_(limits) {}

    // A key not yet held: counted for `consumer` (true), or refused when the store or that
    // consumer's share of it is full (false). A key already held: true, nothing counted -- a
    // replace keeps the creator as owner.
    bool admit(const std::string& key, const std::string& consumer) {
        if (owner_.contains(key)) {
            return true;
        }
        if (owner_.size() >= limits_.max_entries) {
            return false;
        }
        if (!consumer.empty()) {
            const auto it = per_consumer_.find(consumer);
            if ((it == per_consumer_.end() ? 0 : it->second) >= limits_.max_per_consumer) {
                return false;
            }
            ++per_consumer_[consumer];
        }
        owner_.emplace(key, consumer);
        return true;
    }

    // Forgets `key`; a key not held is ignored.
    void release(const std::string& key) {
        const auto it = owner_.find(key);
        if (it == owner_.end()) {
            return;
        }
        if (!it->second.empty()) {
            const auto c = per_consumer_.find(it->second);
            if (c != per_consumer_.end() && --c->second == 0) {
                per_consumer_.erase(c);
            }
        }
        owner_.erase(it);
    }

    [[nodiscard]] std::size_t size() const { return owner_.size(); }

private:
    StoreLimits limits_;
    std::unordered_map<std::string, std::string> owner_;
    std::unordered_map<std::string, std::size_t> per_consumer_;
};

// The expiry time a producer confirms for a subscription: the consumer's proposal when it is no
// later than now + max_lifetime, else now + max_lifetime less a random share of up to a tenth of
// it. TS 29.503 5.5.2.2.2: "The NF Service Producer shall not provide the same expiry time for many
// subscriptions in order to avoid all of them expiring and recreating the subscription at the same
// time."
inline std::chrono::system_clock::time_point
confirm_expiry(std::optional<std::chrono::system_clock::time_point> proposed,
               std::chrono::system_clock::time_point now,
               std::chrono::seconds max_lifetime) {
    const auto latest = now + max_lifetime;
    if (proposed && *proposed <= latest) {
        return *proposed;
    }
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<long long> spread(0, max_lifetime.count() / 10);
    return latest - std::chrono::seconds(spread(rng));
}

} // namespace sbi_core
