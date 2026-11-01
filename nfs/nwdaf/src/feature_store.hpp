#pragma once

// NWDAF's read side of the CHF feature store (ADR-0350) -- TS 23.288 6.2 data collection, for the
// one source this project already has: chf_features.subscriber_features in Doris.
//
// A small MySQL-protocol reader of NWDAF's own, not a reuse of CHF's CdrWriter: CLAUDE.md forbids
// one NF including another's private headers, and the two need different things anyway (CHF
// writes rows; this reads a day). Connection parameters come from config/nwdaf.json, never from
// literals (user mandate).

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

#include "analytics.hpp"

typedef struct st_mysql MYSQL;

namespace nwdaf {

struct FeatureStoreOptions {
    std::string host;
    std::uint16_t port = 0; // from config; a literal default here would be a hardcoded port
    std::string user;
    std::string password;
    std::string database;
    std::int64_t max_rows = 0; // upper bound per read, from config
};

class FeatureStore {
public:
    explicit FeatureStore(const FeatureStoreOptions& options);
    ~FeatureStore();
    FeatureStore(const FeatureStore&) = delete;
    FeatureStore& operator=(const FeatureStore&) = delete;

    bool connected() const { return conn_ != nullptr; }

    // Every subscriber-day row for `date` (YYYY-MM-DD), bounded by max_rows; empty for a day with
    // no rows, which is a real state. A query error is returned, not thrown (REVIEW-5ae0ef1
    // RULE-02): the database's reason.
    std::expected<std::vector<FeatureRow>, std::string> read_day(const std::string& date);

    // The most recent feature_date present ("" when none), so an analytics request with no window
    // still has a real day to compute over. A query error as for read_day.
    std::expected<std::string, std::string> latest_date();

private:
    MYSQL* conn_ = nullptr;
    std::int64_t max_rows_;
};

} // namespace nwdaf
