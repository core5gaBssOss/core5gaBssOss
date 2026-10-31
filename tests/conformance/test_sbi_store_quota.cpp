// REVIEW-5ae0ef1 SUS-02 (ADR-0535): sbi_core::StoreQuota bounds an in-memory store per store and
// per access-token sub; sbi_core::confirm_expiry caps a subscription's expiry at the operator's
// maximum lifetime and spreads the times it sets (TS 29.503 5.5.2.2.2).
#include "sbi_core/store_quota.hpp"

#include <chrono>
#include <optional>

#include <gtest/gtest.h>

namespace {

using sbi_core::StoreLimits;
using sbi_core::StoreQuota;
using std::chrono::hours;
using std::chrono::seconds;
using std::chrono::system_clock;

} // namespace

TEST(SbiStoreQuota, AStoreTakesNoMoreThanItsBound) {
    StoreQuota quota(StoreLimits{.max_entries = 2, .max_per_consumer = 10});
    EXPECT_TRUE(quota.admit("a", "nf-1"));
    EXPECT_TRUE(quota.admit("b", "nf-2"));
    EXPECT_FALSE(quota.admit("c", "nf-3"));
    quota.release("a");
    EXPECT_TRUE(quota.admit("c", "nf-3"));
    EXPECT_EQ(quota.size(), 2U);
}

TEST(SbiStoreQuota, OneConsumerCannotTakeMoreThanItsShare) {
    StoreQuota quota(StoreLimits{.max_entries = 10, .max_per_consumer = 1});
    EXPECT_TRUE(quota.admit("k1", "nf-a"));
    EXPECT_FALSE(quota.admit("k2", "nf-a"));
    EXPECT_TRUE(quota.admit("k3", "nf-b")) << "another consumer is not affected";
    quota.release("k1");
    EXPECT_TRUE(quota.admit("k2", "nf-a")) << "a released entry frees the consumer's share";
}

TEST(SbiStoreQuota, ReplacingAHeldKeyCountsNothing) {
    StoreQuota quota(StoreLimits{.max_entries = 1, .max_per_consumer = 1});
    EXPECT_TRUE(quota.admit("k1", "nf-a"));
    EXPECT_TRUE(quota.admit("k1", "nf-a"));
    EXPECT_TRUE(quota.admit("k1", "nf-b")) << "a replace keeps the creator as owner";
    EXPECT_EQ(quota.size(), 1U);
    quota.release("k1");
    quota.release("k1"); // a key no longer held is ignored
    EXPECT_EQ(quota.size(), 0U);
}

TEST(SbiStoreQuota, ARequestWithoutATokenCountsOnlyTowardTheStoreBound) {
    StoreQuota quota(StoreLimits{.max_entries = 2, .max_per_consumer = 0});
    EXPECT_TRUE(quota.admit("k1", ""));
    EXPECT_FALSE(quota.admit("k2", "nf-a"));
    EXPECT_TRUE(quota.admit("k2", ""));
    EXPECT_FALSE(quota.admit("k3", ""));
}

TEST(SbiConfirmExpiry, AProposalWithinTheLifetimeIsKept) {
    const auto now = system_clock::now();
    const auto proposed = now + hours(1);
    EXPECT_EQ(sbi_core::confirm_expiry(proposed, now, hours(24)), proposed);
    EXPECT_EQ(sbi_core::confirm_expiry(now - seconds(5), now, hours(24)), now - seconds(5));
}

TEST(SbiConfirmExpiry, ALaterOrMissingProposalGetsTheMaximumLessASpread) {
    const auto now = system_clock::now();
    for (const auto proposed : {std::optional<system_clock::time_point>(now + hours(24 * 10)),
                                std::optional<system_clock::time_point>()}) {
        for (int i = 0; i < 50; ++i) {
            const auto confirmed = sbi_core::confirm_expiry(proposed, now, hours(24));
            EXPECT_LE(confirmed, now + hours(24));
            EXPECT_GE(confirmed, now + hours(24) - seconds(hours(24)) / 10);
        }
    }
}

// ADR-0536: the reason for a refusal, so an API that defines its own cause for a consumer over its
// share (TS 29.122 QUOTA_EXCEEDED) can send it, and a full store is told apart.
TEST(SbiStoreQuota, ARefusalSaysWhetherTheStoreOrTheConsumerIsFull) {
    StoreQuota quota(StoreLimits{.max_entries = 2, .max_per_consumer = 1});
    EXPECT_FALSE(quota.try_admit("k1", "nf-a").has_value());
    EXPECT_EQ(quota.try_admit("k2", "nf-a"), sbi_core::Refusal::consumer_full);
    EXPECT_FALSE(quota.try_admit("k2", "nf-b").has_value());
    EXPECT_EQ(quota.try_admit("k3", "nf-c"), sbi_core::Refusal::store_full);
    EXPECT_FALSE(quota.try_admit("k1", "nf-c").has_value()) << "a held key is never refused";
}
