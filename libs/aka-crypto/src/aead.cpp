#include "aka_crypto/aead.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <memory>

namespace aka_crypto {

namespace {

constexpr std::size_t kIvLen = 12;
constexpr std::size_t kTagLen = 16;

struct CtxFree {
    void operator()(EVP_CIPHER_CTX* ctx) const { EVP_CIPHER_CTX_free(ctx); }
};
using Ctx = std::unique_ptr<EVP_CIPHER_CTX, CtxFree>;

} // namespace

std::optional<std::vector<uint8_t>>
aes256gcm_seal(const Aes256Key& key, std::span<const uint8_t> plaintext, std::string_view aad) {
    GcmIv iv{};
    if (RAND_bytes(iv.data(), static_cast<int>(iv.size())) != 1) {
        return std::nullopt;
    }
    return aes256gcm_seal(key, iv, plaintext, aad);
}

std::optional<std::vector<uint8_t>> aes256gcm_seal(const Aes256Key& key,
                                                   const GcmIv& iv,
                                                   std::span<const uint8_t> plaintext,
                                                   std::string_view aad) {
    std::vector<uint8_t> out(iv.begin(), iv.end());
    out.resize(kIvLen + plaintext.size() + kTagLen);
    uint8_t* ct = out.data() + kIvLen;
    Ctx ctx(EVP_CIPHER_CTX_new());
    int len = 0;
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, kIvLen, nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1 ||
        EVP_EncryptUpdate(ctx.get(),
                          nullptr,
                          &len,
                          reinterpret_cast<const unsigned char*>(aad.data()),
                          static_cast<int>(aad.size())) != 1 ||
        (!plaintext.empty() &&
         EVP_EncryptUpdate(
             ctx.get(), ct, &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1) ||
        EVP_EncryptFinal_ex(ctx.get(), ct + plaintext.size(), &len) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, kTagLen, ct + plaintext.size()) != 1) {
        return std::nullopt;
    }
    return out;
}

std::optional<std::vector<uint8_t>>
aes256gcm_open(const Aes256Key& key, std::span<const uint8_t> sealed, std::string_view aad) {
    if (sealed.size() < kIvLen + kTagLen) {
        return std::nullopt;
    }
    const auto ct = sealed.subspan(kIvLen, sealed.size() - kIvLen - kTagLen);
    std::array<uint8_t, kTagLen> tag{};
    std::copy(sealed.end() - kTagLen, sealed.end(), tag.begin());
    std::vector<uint8_t> out(ct.size());
    Ctx ctx(EVP_CIPHER_CTX_new());
    int len = 0;
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, kIvLen, nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), sealed.data()) != 1 ||
        EVP_DecryptUpdate(ctx.get(),
                          nullptr,
                          &len,
                          reinterpret_cast<const unsigned char*>(aad.data()),
                          static_cast<int>(aad.size())) != 1 ||
        (!ct.empty() &&
         EVP_DecryptUpdate(ctx.get(), out.data(), &len, ct.data(), static_cast<int>(ct.size())) !=
             1) ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, kTagLen, tag.data()) != 1 ||
        EVP_DecryptFinal_ex(ctx.get(), out.data() + ct.size(), &len) != 1) {
        return std::nullopt;
    }
    return out;
}

} // namespace aka_crypto
