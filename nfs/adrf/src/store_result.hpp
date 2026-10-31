#pragma once

#include <expected>
#include <string>

namespace adrf {

// What a RecordStore / ModelStore call gives back: the value, or why the database could not give
// it. A database that is down or refuses a statement is a recoverable condition the handler maps to
// its ProblemDetails, so it is returned, not thrown (CLAUDE.md engineering rules; REVIEW-5ae0ef1
// RULE-02).
template <typename T> using StoreResult = std::expected<T, std::string>;

} // namespace adrf
