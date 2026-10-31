// The IPv4 address an NF advertises in its NRF NFProfile (`ipv4Addresses`), which consumers that
// discover it parse as a literal (the SMF sends PFCP to the UPF's; the MTLF dereferences the
// AnLF's). ADR-0466 for the UPF (also its PFCP Node ID); every NF since ADR-0518 -- before it, 23
// NFs registered a hardcoded 127.0.0.1, unreachable from any other host, container or pod.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <tl/expected.hpp>

namespace sbi_core {

using Ipv4 = std::array<std::uint8_t, 4>;

// `setting` is the NF's config `advertised_ipv4` (env <NF>_ADVERTISED_IPV4):
//   - a literal dotted-quad IPv4 (not 0.0.0.0): used as given;
//   - "auto": the local address the kernel would use to reach the NRF named in `nrf_base_url`
//     (UDP connect() + getsockname(); sends no packet). 127.0.0.1 on a host lab where the NRF is on
//     loopback, the container's own address in Compose/Kubernetes.
// Anything else, and any failure to find an address, is an error: the caller must not start with a
// guessed address, because a wrong one is silent (a consumer just never reaches the NF).
tl::expected<Ipv4, std::string> resolve_advertised_ipv4(const std::string& setting,
                                                        const std::string& nrf_base_url);

std::string ipv4_to_string(const Ipv4& ip);

// resolve_advertised_ipv4 as a dotted quad, or nf_config::fatal() naming `nf` -- for an NF's
// startup, where no address means no usable NRF registration (ADR-0518).
std::string advertised_ipv4_or_die(std::string_view nf,
                                   const std::string& setting,
                                   const std::string& nrf_base_url);

} // namespace sbi_core
