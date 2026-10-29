#pragma once

// ADR-0525: UE IPv4 address allocation (TS 23.501 5.8.2.2.1, "UE IP address allocation by the
// SMF"), one operator-configured pool per DNN (config/smf.json `ue_ipv4_pools`, CIDR strings).
// The lowest free host address is handed out; the network and broadcast addresses never are.
// Released at PDU session release (TS 23.502 4.3.4.2 step 2). In memory, like the SM contexts.

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>

namespace smf {

class UeIpPool {
public:
    // `pools`: DNN -> "a.b.c.d/len" (len 8..30). Throws std::invalid_argument on a malformed entry
    // (a configuration error, reported at startup).
    explicit UeIpPool(const std::map<std::string, std::string>& pools);

    // A free address of the DNN's pool; std::nullopt when the DNN has no pool or it is exhausted.
    std::optional<std::array<std::uint8_t, 4>> allocate(const std::string& dnn);
    void release(const std::string& dnn, std::array<std::uint8_t, 4> address);

    static std::string to_string(std::array<std::uint8_t, 4> address);
    static std::optional<std::array<std::uint8_t, 4>> parse(const std::string& dotted);

private:
    struct Pool {
        std::uint32_t first = 0; // first host address
        std::uint32_t last = 0;  // last host address
        std::set<std::uint32_t> in_use;
    };
    std::mutex mutex_;
    std::map<std::string, Pool> pools_;
};

} // namespace smf
