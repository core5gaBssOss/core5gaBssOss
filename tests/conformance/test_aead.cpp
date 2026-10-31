// libs/aka-crypto AES-256-GCM (ADR-0514, REVIEW-5ae0ef1 SEC-04). Known answers are test cases 13,
// 14 and 16 of McGrew and Viega, "The Galois/Counter Mode of Operation (GCM)" (the 256-bit key
// cases), each re-derived independently with Python `cryptography` 46.0.5 before this file was
// written.

#include <string>

#include "aka_crypto/aead.hpp"
#include "aka_crypto/hex.hpp"

#include <gtest/gtest.h>

namespace {

std::vector<uint8_t> hex(const std::string& s) {
    return aka_crypto::from_hex(s).value_or(std::vector<uint8_t>{});
}

std::string aad_of(const std::vector<uint8_t>& v) {
    return std::string(v.begin(), v.end());
}

} // namespace

TEST(Aes256Gcm, KnownAnswerTestCases13And14) {
    const aka_crypto::Aes256Key key{};
    const aka_crypto::GcmIv iv{};
    const auto tc13 = aka_crypto::aes256gcm_seal(key, iv, {}, "");
    ASSERT_TRUE(tc13);
    EXPECT_EQ(aka_crypto::to_hex(*tc13),
              "000000000000000000000000530f8afbc74536b9a963b4f1c4cb738b");
    const std::vector<uint8_t> zeros(16, 0);
    const auto tc14 = aka_crypto::aes256gcm_seal(key, iv, zeros, "");
    ASSERT_TRUE(tc14);
    EXPECT_EQ(aka_crypto::to_hex(*tc14),
              "000000000000000000000000"
              "cea7403d4d606b6e074ec5d3baf39d18d0d1c8a799996bf0265b98b5d48ab919");
}

TEST(Aes256Gcm, KnownAnswerTestCase16WithAad) {
    const auto key = aka_crypto::from_hex<32>(
        "feffe9928665731c6d6a8f9467308308feffe9928665731c6d6a8f9467308308");
    const auto iv = aka_crypto::from_hex<12>("cafebabefacedbaddecaf888");
    ASSERT_TRUE(key && iv);
    const auto pt = hex("d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a72"
                        "1c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b39");
    const auto aad = aad_of(hex("feedfacedeadbeeffeedfacedeadbeefabaddad2"));
    const auto sealed = aka_crypto::aes256gcm_seal(*key, *iv, pt, aad);
    ASSERT_TRUE(sealed);
    EXPECT_EQ(aka_crypto::to_hex(*sealed),
              "cafebabefacedbaddecaf888"
              "522dc1f099567d07f47f37a32a84427d643a8cdcbfe5c0c97598a2bd2555d1aa"
              "8cb08e48590dbb3da7b08b1056828838c5f61e6393ba7a0abcc9f662"
              "76fc6ece0f4e1768cddf8853bb2d551b");
    const auto opened = aka_crypto::aes256gcm_open(*key, *sealed, aad);
    ASSERT_TRUE(opened);
    EXPECT_EQ(*opened, pt);
}

TEST(Aes256Gcm, RandomIvRoundTripsAndDiffersPerCall) {
    aka_crypto::Aes256Key key{};
    key[0] = 1;
    const std::vector<uint8_t> pt(32, 0xab);
    const auto a = aka_crypto::aes256gcm_seal(key, pt, "aanf:akid:x");
    const auto b = aka_crypto::aes256gcm_seal(key, pt, "aanf:akid:x");
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->size(), 12U + 32U + 16U);
    EXPECT_NE(*a, *b);
    EXPECT_EQ(aka_crypto::aes256gcm_open(key, *a, "aanf:akid:x"), pt);
}

TEST(Aes256Gcm, WrongKeyAadOrAnyFlippedOctetDoesNotOpen) {
    aka_crypto::Aes256Key key{};
    key[31] = 7;
    const std::vector<uint8_t> pt(32, 0x5a);
    const auto sealed = aka_crypto::aes256gcm_seal(key, pt, "record-1");
    ASSERT_TRUE(sealed);
    auto other = key;
    other[0] ^= 1;
    EXPECT_FALSE(aka_crypto::aes256gcm_open(other, *sealed, "record-1"));
    EXPECT_FALSE(aka_crypto::aes256gcm_open(key, *sealed, "record-2"));
    for (std::size_t i = 0; i < sealed->size(); ++i) {
        auto bad = *sealed;
        bad[i] ^= 0x80;
        EXPECT_FALSE(aka_crypto::aes256gcm_open(key, bad, "record-1")) << "octet " << i;
    }
    EXPECT_FALSE(aka_crypto::aes256gcm_open(key, std::vector<uint8_t>(27, 0), "record-1"));
}
