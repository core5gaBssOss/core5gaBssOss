#include "akma_store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

#include "aka_crypto/hex.hpp"

namespace aanf {

namespace {
std::string akid_key(const std::string& a_kid) {
    return "aanf:akid:" + a_kid;
}
std::string supi_key(const std::string& supi) {
    return "aanf:supi:" + supi;
}
// The AAD: the record's key name and its SUPI.
std::string aad(const std::string& a_kid, const std::string& supi) {
    return akid_key(a_kid) + '\n' + supi;
}
} // namespace

bool AkmaStore::put(const AkmaContext& ctx) {
    const auto sealed = aka_crypto::aes256gcm_seal(kek_, ctx.kakma, aad(ctx.a_kid, ctx.supi));
    if (!sealed) {
        return false;
    }
    // Replace the SUPI's previous A-KID, if any, in one transaction.
    const auto old = redis_->get(supi_key(ctx.supi));
    auto tx = redis_->transaction();
    if (old && *old != ctx.a_kid) {
        tx.del(akid_key(*old));
    }
    tx.set(akid_key(ctx.a_kid),
           nlohmann::json{{"supi", ctx.supi}, {"kakmaSealed", aka_crypto::to_hex(*sealed)}}.dump());
    tx.set(supi_key(ctx.supi), ctx.a_kid);
    tx.exec();
    return true;
}

std::optional<AkmaContext> AkmaStore::by_akid(const std::string& a_kid) {
    const auto raw = redis_->get(akid_key(a_kid));
    if (!raw) {
        return std::nullopt;
    }
    const auto doc = nlohmann::json::parse(*raw, nullptr, false);
    if (doc.is_discarded()) {
        return std::nullopt;
    }
    const auto supi = doc.value("supi", std::string{});
    const auto sealed = aka_crypto::from_hex(doc.value("kakmaSealed", std::string{}));
    if (!sealed) {
        return std::nullopt;
    }
    const auto opened = aka_crypto::aes256gcm_open(kek_, *sealed, aad(a_kid, supi));
    if (!opened || opened->size() != 32) {
        return std::nullopt;
    }
    AkmaContext ctx{supi, a_kid, {}};
    std::copy(opened->begin(), opened->end(), ctx.kakma.begin());
    return ctx;
}

bool AkmaStore::remove_supi(const std::string& supi) {
    const auto a_kid = redis_->get(supi_key(supi));
    if (!a_kid) {
        return false;
    }
    redis_->del(akid_key(*a_kid));
    redis_->del(supi_key(supi));
    return true;
}

} // namespace aanf
