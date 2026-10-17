#pragma once

// ADR-0499 phase 4: gives a test's client the access token for each API it calls, the way an NF's
// clients get theirs (sbi_core::TokenSource, phase 3), so a request a test sends without an
// Authorization header still reaches an enforcing NF. A request with its own header keeps it.
// Tokens are requested as `test-client` with the hello-nf certificate, accepted only while the
// NRF's token check is off (config/nrf.json); phase 5 replaces that identity here, in one place.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/oauth2_client.hpp"

#include <chrono>

namespace nf_test {

inline void attach_test_tokens(sbi_core::http2::Client& client) {
    static sbi_core::http2::Client nrf_client(sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    });
    // No back-off: a test's first request may race the NRF's start.
    static sbi_core::TokenSource tokens(
        nrf_client, "https://127.0.0.1:7777/oauth2/token", "test-client", std::chrono::seconds(0));
    sbi_core::attach_tokens(client, tokens);
}

} // namespace nf_test
