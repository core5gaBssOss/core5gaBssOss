#include "aka_crypto/akma.hpp"

#include <cctype>

#include "aka_crypto/hex.hpp"

namespace aka_crypto {

std::string make_a_kid(const std::string& routing_indicator,
                       const ATid& a_tid,
                       const std::string& mcc,
                       const std::string& mnc) {
    std::string tid = to_hex(a_tid);
    for (auto& c : tid) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return "rid" + routing_indicator + ".tid" + tid + "@5gc.mnc" +
           (mnc.size() == 2 ? "0" + mnc : mnc) + ".mcc" + mcc + ".3gppnetwork.org";
}

std::optional<std::vector<uint8_t>> af_id_octets(const std::string& af_id) {
    const auto dot = af_id.rfind('.');
    if (dot == std::string::npos || dot == 0 || af_id.size() - dot - 1 != 10) {
        return std::nullopt;
    }
    const auto proto = from_hex<5>(af_id.substr(dot + 1));
    if (!proto) {
        return std::nullopt;
    }
    std::vector<uint8_t> out(af_id.begin(), af_id.begin() + static_cast<std::ptrdiff_t>(dot));
    out.insert(out.end(), proto->begin(), proto->end());
    return out;
}

} // namespace aka_crypto
