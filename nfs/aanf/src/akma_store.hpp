#pragma once

#include <memory>
#include <optional>
#include <string>
#include <sw/redis++/redis++.h>

#include "aka_crypto/kdf.hpp"

// The AAnF's AKMA contexts (TS 33.535 6.1 step 4, 6.6): SUPI, A-KID, KAKMA -- in Valkey so every
// replica answers for every UE. Keys: "aanf:akid:<A-KID>" -> {supi, kakma}, "aanf:supi:<SUPI>" ->
// A-KID. A new registration for a SUPI replaces the old A-KID (6.1 NOTE 1a).

namespace aanf {

struct AkmaContext {
    std::string supi;
    std::string a_kid;
    aka_crypto::Kakma kakma{};
};

class AkmaStore {
public:
    explicit AkmaStore(std::shared_ptr<sw::redis::Redis> redis) : redis_(std::move(redis)) {}
    void put(const AkmaContext& ctx);
    std::optional<AkmaContext> by_akid(const std::string& a_kid);
    bool remove_supi(const std::string& supi);

private:
    std::shared_ptr<sw::redis::Redis> redis_;
};

} // namespace aanf
