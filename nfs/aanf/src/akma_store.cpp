#include "akma_store.hpp"

#include <nlohmann/json.hpp>

#include "aka_crypto/hex.hpp"

namespace aanf {

namespace {
std::string akid_key(const std::string& a_kid) {
    return "aanf:akid:" + a_kid;
}
std::string supi_key(const std::string& supi) {
    return "aanf:supi:" + supi;
}
} // namespace

void AkmaStore::put(const AkmaContext& ctx) {
    // Replace the SUPI's previous A-KID, if any, in one transaction.
    const auto old = redis_->get(supi_key(ctx.supi));
    auto tx = redis_->transaction();
    if (old && *old != ctx.a_kid) {
        tx.del(akid_key(*old));
    }
    tx.set(akid_key(ctx.a_kid),
           nlohmann::json{{"supi", ctx.supi}, {"kakma", aka_crypto::to_hex(ctx.kakma)}}.dump());
    tx.set(supi_key(ctx.supi), ctx.a_kid);
    tx.exec();
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
    const auto kakma = aka_crypto::from_hex<32>(doc.value("kakma", std::string{}));
    if (!kakma) {
        return std::nullopt;
    }
    return AkmaContext{doc.value("supi", std::string{}), a_kid, *kakma};
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
