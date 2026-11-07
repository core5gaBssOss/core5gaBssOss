#pragma once

// ADR-0565: the UDR refuses writes under subscription-data/{ueId} for a user it has not
// provisioned, so a test that writes for its own SUPI onboards that SUPI first, the way a real
// subscriber is onboarded: bss/provisioning's UdrAdapter over the UDR's OAM provisioning API
// (ADR-0382), as the "provisioning" mTLS identity config/udr.json allows.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <string>

#include "udr_adapter.hpp"

namespace nf_test {

// Test-only SIM keys, generated for this file -- not the TS 35.207 test-set values the UDR seeds.
inline constexpr const char* kProvisionedTestK = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";
inline constexpr const char* kProvisionedTestOpc = "a0b1c2d3e4f5061728394a5b6c7d8e9f";

// Onboards `supi` (imsi- form) at the UDR on 127.0.0.1:7781; `msisdn` (digits, may be empty)
// becomes the "msisdn-" GPSI in its am_data. The PUT replaces an earlier run's documents.
inline tl::expected<provisioning::UdrProvisionOutcome, std::string>
provision_subscriber(const std::string& supi, const std::string& msisdn) {
    using nlohmann::json;
    provisioning::UdrAdapter adapter(
        provisioning::parse_udr_adapter_config(json{
            {"udr_base_url", "https://127.0.0.1:7781"},
            {"serving_plmn_id", "99970"},
            {"authentication",
             {{"method", "5G_AKA"}, {"management_field", "8000"}, {"initial_sqn", "000000000000"}}},
            {"network_profiles",
             {{"default",
               {{"snssais", json::array({{{"sst", 1}, {"sd", "000001"}}})},
                {"dnn", "internet"},
                {"ue_ambr", {{"uplink", "1 Gbps"}, {"downlink", "1 Gbps"}}}}}}},
        }),
        sbi_core::http2::TlsConfig{
            .cert_path = CERTS_DIR "/provisioning/cert.pem",
            .key_path = CERTS_DIR "/provisioning/key.pem",
            .ca_path = CERTS_DIR "/ca/ca.crt",
        });
    return adapter.provision(provisioning::SubscriberSpec{
        supi,
        msisdn,
        "po-test",
        provisioning::SimCredentials{kProvisionedTestK, kProvisionedTestOpc, "000000000020"}});
}

} // namespace nf_test
