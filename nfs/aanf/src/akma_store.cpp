#include "akma_store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <vector>

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
    // SEC-15 (ADR-0549): the read of the SUPI's old A-KID and the writes run as one Lua script,
    // so two registrations for one SUPI cannot each delete only the A-KID they read and leave the
    // other's record orphaned (TS 33.535 6.1 NOTE 1a: the new context replaces the old one). An
    // A-KID record that belonged to another SUPI is taken over, and that SUPI's index entry is
    // dropped with it, so no index entry names a record it does not own. Every key the script
    // touches is under "aanf:", the prefix the AAnF's Valkey ACL user is limited to.
    static const std::string kPut = R"lua(
local old = redis.call('GET', KEYS[1])
if old and old ~= ARGV[1] then redis.call('DEL', 'aanf:akid:' .. old) end
local prev = redis.call('GET', KEYS[2])
if prev then
  local ok, doc = pcall(cjson.decode, prev)
  if ok and type(doc) == 'table' and type(doc.supi) == 'string' and doc.supi ~= ARGV[3] then
    local other = 'aanf:supi:' .. doc.supi
    if redis.call('GET', other) == ARGV[1] then redis.call('DEL', other) end
  end
end
redis.call('SET', KEYS[2], ARGV[2])
redis.call('SET', KEYS[1], ARGV[1])
return 1
)lua";
    const std::vector<std::string> keys{supi_key(ctx.supi), akid_key(ctx.a_kid)};
    const std::vector<std::string> args{
        ctx.a_kid,
        nlohmann::json{{"supi", ctx.supi}, {"kakmaSealed", aka_crypto::to_hex(*sealed)}}.dump(),
        ctx.supi};
    redis_->eval<long long>(kPut, keys.begin(), keys.end(), args.begin(), args.end());
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
    // One script, as in put(): a registration between the read and the deletes cannot leave its
    // new A-KID record behind with no index entry.
    static const std::string kRemove = R"lua(
local a = redis.call('GET', KEYS[1])
if not a then return 0 end
redis.call('DEL', 'aanf:akid:' .. a, KEYS[1])
return 1
)lua";
    const std::vector<std::string> keys{supi_key(supi)};
    const std::vector<std::string> args;
    return redis_->eval<long long>(kRemove, keys.begin(), keys.end(), args.begin(), args.end()) ==
           1;
}

} // namespace aanf
