// REVIEW-5ae0ef1 DOC-03 part 2 (ADR-0495): independent known-answer vectors for the AKMA key
// derivations of TS 33.535 j00 Annex A, built on the generic KDF of TS 33.220 j10 Annex B.2.0:
//
//   S           = FC || P0 || L0 || P1 || L1 || ...      (Li = 2-octet big-endian length of Pi)
//   derived key = HMAC-SHA-256(Key, S)
//
//   A.2  KAKMA: FC=0x80, P0="AKMA",  P1=SUPI,  Key=KAUSF
//   A.3  A-TID: FC=0x81, P0="A-TID", P1=SUPI,  Key=KAUSF
//   A.4  KAF:   FC=0x82, P0=AF_ID,             Key=KAKMA
//               AF_ID = FQDN of the AF || Ua* security protocol id (5 octets, TS 33.220 Annex H)
//
// SUPI encoding: TS 33.535 A.2/A.3 say SUPI is "the same value as parameter P0 in Annex A.7.0 of
// TS 33.501", i.e. for an IMSI-type SUPI the IMSI digits of TS 23.003 2.2 as a character string
// (TS 33.220 B.2.1.2: UTF-8 + NFKC; identical to ASCII for these inputs). The FQDN is ASCII, so the
// same holds for it.
//
// The expected values below were NOT produced by this project's code. They were computed with the
// Python stdlib (hmac + hashlib), with every S spelled out octet by octet:
//
//   KAUSF = bytes.fromhex("0123456789abcdeffedcba98765432100f1e2d3c4b5a69788796a5b4c3d2e1f0")
//   supi  = b"001010000000001"                       # 15 octets -> L1 = 00 0f
//   S_kakma = b"\x80" + b"AKMA"  + b"\x00\x04" + supi + b"\x00\x0f"
//   S_atid  = b"\x81" + b"A-TID" + b"\x00\x05" + supi + b"\x00\x0f"
//   KAKMA = hmac.new(KAUSF, S_kakma, hashlib.sha256).digest()
//   A_TID = hmac.new(KAUSF, S_atid,  hashlib.sha256).digest()
//   af_id = b"af1.example.com" + bytes([0x01,0x00,0x00,0x00,0x07])  # 20 octets -> L0 = 00 14
//   S_kaf = b"\x82" + af_id + b"\x00\x14"
//   KAF   = hmac.new(KAKMA, S_kaf, hashlib.sha256).digest()
//
// and cross-checked with `xxd -r -p | openssl dgst -sha256 -mac HMAC -macopt hexkey:<Key>` on each
// S. Ua* id 0x01 00 00 00 07 is TS 33.220 H.3's "shared key TLS 1.3" identifier. The inputs (KAUSF,
// SUPI, FQDN) are arbitrary test values: 3GPP publishes no official AKMA test vectors in
// TS 33.535 j00, so these are hand-computed, not 3GPP-published, known answers.

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "aka_crypto/akma.hpp"
#include "aka_crypto/kdf.hpp"

#include <gtest/gtest.h>

namespace {

// Deliberately local (not aka_crypto::from_hex): nothing in the expected-value path goes through
// the library under test.
std::vector<uint8_t> hex(const std::string& text) {
    auto nibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') {
            return static_cast<uint8_t>(c - '0');
        }
        return static_cast<uint8_t>(c - 'a' + 10);
    };
    std::vector<uint8_t> out;
    for (size_t i = 0; i + 1 < text.size(); i += 2) {
        out.push_back(static_cast<uint8_t>((nibble(text[i]) << 4) | nibble(text[i + 1])));
    }
    return out;
}

template <size_t N> std::array<uint8_t, N> arr(const std::string& text) {
    const auto v = hex(text);
    std::array<uint8_t, N> out{};
    std::copy(v.begin(), v.end(), out.begin());
    return out;
}

template <size_t N> std::vector<uint8_t> vec(const std::array<uint8_t, N>& a) {
    return {a.begin(), a.end()};
}

std::vector<uint8_t> hmac_sha256(const std::vector<uint8_t>& key, const std::vector<uint8_t>& s) {
    std::vector<uint8_t> out(32);
    unsigned int len = 0;
    HMAC(EVP_sha256(),
         key.data(),
         static_cast<int>(key.size()),
         s.data(),
         s.size(),
         out.data(),
         &len);
    out.resize(len);
    return out;
}

constexpr const char* kKausf = "0123456789abcdeffedcba98765432100f1e2d3c4b5a69788796a5b4c3d2e1f0";
constexpr const char* kSupi = "001010000000001";

// 80 | 41 4b 4d 41 ("AKMA") | 00 04 | 30 30 31 30 31 30 30 30 30 30 30 30 30 30 31 (SUPI) | 00 0f
constexpr const char* kSKakma = "80414b4d410004303031303130303030303030303031000f";
constexpr const char* kKakma = "321baa44a438e4278b93ac0d067469b423d50d7edb72070dd7bb977d84beb741";

// 81 | 41 2d 54 49 44 ("A-TID") | 00 05 | SUPI (as above) | 00 0f
constexpr const char* kSATid = "81412d5449440005303031303130303030303030303031000f";
constexpr const char* kATid = "3855c8474cff2448781f8d7b178e626791130c2d89bc1a39f1cb836873885f05";

// AF_ID = 61 66 31 2e 65 78 61 6d 70 6c 65 2e 63 6f 6d ("af1.example.com") | 01 00 00 00 07
constexpr const char* kAfId = "6166312e6578616d706c652e636f6d0100000007";
// 82 | AF_ID (20 octets) | 00 14
constexpr const char* kSKaf = "826166312e6578616d706c652e636f6d01000000070014";
constexpr const char* kKaf = "97a88d6e8d852a29a1fb2b69416bc8ab112be78f35979fe119bdd9ebd4172660";

} // namespace

// The constants are self-consistent: HMAC-SHA-256 (OpenSSL directly, no aka_crypto) of each
// spelled-out S under its Key gives the stated key.
TEST(AkmaKdfVectors, SpelledOutInputStringsGiveTheStatedKeys) {
    EXPECT_EQ(hmac_sha256(hex(kKausf), hex(kSKakma)), hex(kKakma));
    EXPECT_EQ(hmac_sha256(hex(kKausf), hex(kSATid)), hex(kATid));
    EXPECT_EQ(hmac_sha256(hex(kKakma), hex(kSKaf)), hex(kKaf));
}

// TS 33.535 A.2.
TEST(AkmaKdfVectors, KakmaMatchesAnnexA2Vector) {
    const auto kakma = aka_crypto::derive_kakma(arr<32>(kKausf), kSupi);
    EXPECT_EQ(vec(kakma), hex(kKakma));
}

// TS 33.535 A.3.
TEST(AkmaKdfVectors, ATidMatchesAnnexA3Vector) {
    const auto a_tid = aka_crypto::derive_a_tid(arr<32>(kKausf), kSupi);
    EXPECT_EQ(vec(a_tid), hex(kATid));
}

// TS 33.535 A.4, keyed with the hand-computed KAKMA (not the library's own output).
TEST(AkmaKdfVectors, KafMatchesAnnexA4Vector) {
    const auto kaf = aka_crypto::derive_kaf(arr<32>(kKakma), hex(kAfId));
    EXPECT_EQ(vec(kaf), hex(kKaf));
}

// The AAnF path: TS 29.522 AfId "<FQDN>.<10 hex digits>" -> AF_ID octets (FQDN || 5-octet Ua* id)
// -> KAF, chained from KAUSF.
TEST(AkmaKdfVectors, AfIdStringParsesToAnnexA4OctetsAndChainsToKaf) {
    const auto af_id = aka_crypto::af_id_octets("af1.example.com.0100000007");
    ASSERT_TRUE(af_id.has_value());
    EXPECT_EQ(*af_id, hex(kAfId));
    const auto kakma = aka_crypto::derive_kakma(arr<32>(kKausf), kSupi);
    EXPECT_EQ(vec(aka_crypto::derive_kaf(kakma, *af_id)), hex(kKaf));
}
