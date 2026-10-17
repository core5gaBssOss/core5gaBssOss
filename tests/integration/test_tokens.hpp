#pragma once

// ADR-0499 phase 4: gives a test's client the access token for each API it calls, the way an NF's
// clients get theirs (sbi_core::TokenSource, phase 3), so a request a test sends without an
// Authorization header still reaches an enforcing NF. A request with its own header keeps it.
// Tokens are requested as `test-client` with the hello-nf certificate, accepted only while the
// NRF's token check is off (config/nrf.json); phase 5 replaces that identity here, in one place.
//
// The NEF's northbound APIs (3gpp-*, TS 29.122/29.522) declare OAuth2 with no scope and name no
// producer NF type, so the TokenSource gives them none; the NEF accepts any NRF token for itself
// there (decided by the user, 2026-10-17). For those, the test asks for a token for the NEF with
// the YAML's scope where one is declared, else the apiName as the (mandatory) scope value -- a
// label only, no YAML defines it.

#include "sbi_core/api_catalog.hpp"
#include "sbi_core/http2_client.hpp"
#include "sbi_core/oauth2_client.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace nf_test {

inline void attach_test_tokens(sbi_core::http2::Client& client) {
    static sbi_core::http2::Client nrf_client(sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    });
    static const std::string kTokenUrl = "https://127.0.0.1:7777/oauth2/token";
    // No back-off: a test's first request may race the NRF's start.
    static sbi_core::TokenSource tokens(
        nrf_client, kTokenUrl, "test-client", std::chrono::seconds(0));
    static std::mutex northbound_mutex;
    static std::map<std::string, std::unique_ptr<sbi_core::OAuth2Client>> northbound;
    client.set_token_source([](const std::string& url) -> std::optional<std::string> {
        if (auto token = tokens.token_for(url)) {
            return token;
        }
        const auto api = sbi_core::api_catalog::api_name_of(url);
        if (api.rfind("3gpp-", 0) != 0) {
            return std::nullopt;
        }
        sbi_core::OAuth2Client* oauth = nullptr;
        {
            std::lock_guard lock(northbound_mutex);
            auto& slot = northbound[api];
            if (!slot) {
                // The YAML's scope where it declares one (3gpp-akma: nnef-akma:gpsi-access).
                const auto info = sbi_core::api_catalog::find(api);
                const std::string scope =
                    info && !info->scope.empty() ? std::string(info->scope) : api;
                slot = std::make_unique<sbi_core::OAuth2Client>(
                    nrf_client, kTokenUrl, "test-client", scope, "NEF");
            }
            oauth = slot.get();
        }
        auto token = oauth->get_bearer_token();
        return token ? std::optional<std::string>(*token) : std::nullopt;
    });
}

} // namespace nf_test
