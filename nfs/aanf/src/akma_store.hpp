#pragma once

#include <memory>
#include <optional>
#include <string>
#include <sw/redis++/redis++.h>

#include "aka_crypto/aead.hpp"
#include "aka_crypto/kdf.hpp"

// The AAnF's AKMA contexts (TS 33.535 6.1 step 4, 6.6): SUPI, A-KID, KAKMA -- in Valkey so every
// replica answers for every UE. Keys: "aanf:akid:<A-KID>" -> {supi, kakma}, "aanf:supi:<SUPI>" ->
// A-KID. A new registration for a SUPI replaces the old A-KID (6.1 NOTE 1a).
//
// KAKMA is never stored in the clear (TS 33.117 4.2.3.2.3; REVIEW-5ae0ef1 SEC-04, ADR-0514): the
// value holds "kakmaSealed", AES-256-GCM under the AAnF's key-encryption key with the record's
// own key name and SUPI as AAD, so a sealed KAKMA copied into another record does not open. A
// record that does not open (another KEK, tampered, or the pre-ADR-0514 plaintext "kakma") reads
// as absent.

namespace aanf {

struct AkmaContext {
    std::string supi;
    std::string a_kid;
    aka_crypto::Kakma kakma{};
};

class AkmaStore {
public:
    AkmaStore(std::shared_ptr<sw::redis::Redis> redis, const aka_crypto::Aes256Key& kek)
        : redis_(std::move(redis)), kek_(kek) {}
    // false only if sealing failed (OpenSSL); nothing is written then.
    bool put(const AkmaContext& ctx);
    std::optional<AkmaContext> by_akid(const std::string& a_kid);
    bool remove_supi(const std::string& supi);

private:
    std::shared_ptr<sw::redis::Redis> redis_;
    aka_crypto::Aes256Key kek_;
};

} // namespace aanf
