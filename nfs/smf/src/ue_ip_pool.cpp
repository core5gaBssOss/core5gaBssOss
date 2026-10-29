#include "ue_ip_pool.hpp"

#include <sstream>
#include <stdexcept>

namespace smf {

namespace {

std::uint32_t to_u32(std::array<std::uint8_t, 4> a) {
    return (static_cast<std::uint32_t>(a[0]) << 24) | (static_cast<std::uint32_t>(a[1]) << 16) |
           (static_cast<std::uint32_t>(a[2]) << 8) | static_cast<std::uint32_t>(a[3]);
}

std::array<std::uint8_t, 4> from_u32(std::uint32_t v) {
    return {static_cast<std::uint8_t>(v >> 24),
            static_cast<std::uint8_t>(v >> 16),
            static_cast<std::uint8_t>(v >> 8),
            static_cast<std::uint8_t>(v)};
}

} // namespace

std::optional<std::array<std::uint8_t, 4>> UeIpPool::parse(const std::string& dotted) {
    std::array<std::uint8_t, 4> out{};
    std::istringstream in(dotted);
    for (int i = 0; i < 4; ++i) {
        int octet = -1;
        if (!(in >> octet) || octet < 0 || octet > 255) {
            return std::nullopt;
        }
        out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(octet);
        if (i < 3 && in.get() != '.') {
            return std::nullopt;
        }
    }
    if (in.peek() != std::char_traits<char>::eof()) {
        return std::nullopt;
    }
    return out;
}

std::string UeIpPool::to_string(std::array<std::uint8_t, 4> a) {
    return std::to_string(a[0]) + "." + std::to_string(a[1]) + "." + std::to_string(a[2]) + "." +
           std::to_string(a[3]);
}

UeIpPool::UeIpPool(const std::map<std::string, std::string>& pools) {
    for (const auto& [dnn, cidr] : pools) {
        const auto slash = cidr.find('/');
        const auto base = slash == std::string::npos ? std::nullopt : parse(cidr.substr(0, slash));
        int len = -1;
        try {
            len = slash == std::string::npos ? -1 : std::stoi(cidr.substr(slash + 1));
        } catch (const std::exception&) {
            len = -1;
        }
        if (!base.has_value() || len < 8 || len > 30) {
            throw std::invalid_argument("ue_ipv4_pools: '" + cidr + "' for DNN '" + dnn +
                                        "' is not a.b.c.d/len with len 8..30");
        }
        const std::uint32_t mask = len == 0 ? 0 : (0xFFFFFFFFU << (32 - len));
        const std::uint32_t network = to_u32(*base) & mask;
        Pool p;
        p.first = network + 1;
        p.last = (network | ~mask) - 1;
        pools_.emplace(dnn, std::move(p));
    }
}

std::optional<std::array<std::uint8_t, 4>> UeIpPool::allocate(const std::string& dnn) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = pools_.find(dnn);
    if (it == pools_.end()) {
        return std::nullopt;
    }
    auto& p = it->second;
    // Lowest free address: walk the in-use set, which is ordered, for the first gap.
    std::uint32_t candidate = p.first;
    for (const auto used : p.in_use) {
        if (used != candidate) {
            break;
        }
        ++candidate;
    }
    if (candidate > p.last) {
        return std::nullopt;
    }
    p.in_use.insert(candidate);
    return from_u32(candidate);
}

void UeIpPool::release(const std::string& dnn, std::array<std::uint8_t, 4> address) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = pools_.find(dnn); it != pools_.end()) {
        it->second.in_use.erase(to_u32(address));
    }
}

} // namespace smf
