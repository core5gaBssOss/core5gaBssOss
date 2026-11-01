#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// AES-256-GCM (NIST SP 800-38D) for key material held at rest (REVIEW-5ae0ef1 SEC-04,
// ADR-0514): TS 33.117 4.2.3.2.3 -- authentication data shall not be stored in the clear. The
// sealed form is IV (12 octets) || ciphertext || tag (16 octets); the AAD binds the plaintext to
// where it is stored, so a sealed value moved to another record does not open.

namespace aka_crypto {

using Aes256Key = std::array<uint8_t, 32>;
using GcmIv = std::array<uint8_t, 12>;

// A fresh random IV per call (RAND_bytes). nullopt only if OpenSSL fails.
std::optional<std::vector<uint8_t>>
aes256gcm_seal(const Aes256Key& key, std::span<const uint8_t> plaintext, std::string_view aad);

// The same with a caller-chosen IV -- for known-answer tests only; never reuse an IV under a key.
std::optional<std::vector<uint8_t>> aes256gcm_seal(const Aes256Key& key,
                                                   const GcmIv& iv,
                                                   std::span<const uint8_t> plaintext,
                                                   std::string_view aad);

// nullopt if `sealed` is malformed, or the key, AAD or any octet does not authenticate.
std::optional<std::vector<uint8_t>>
aes256gcm_open(const Aes256Key& key, std::span<const uint8_t> sealed, std::string_view aad);

} // namespace aka_crypto
