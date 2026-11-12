#pragma once

// UDR node adapter for the provisioning saga (ADR-0382). Builds one subscriber's 3GPP documents --
// AccessAndMobilitySubscriptionData, SessionManagementSubscriptionData,
// SmfSelectionSubscriptionData (TS29503_Nudm_SDM.yaml), AuthenticationSubscription
// (TS29505_Subscription_Data.yaml), AmPolicyData and SmPolicyData (TS29519_Policy_Data.yaml) -- and
// writes them through the UDR's project-owned OAM provisioning API (PUT
// /oam-provisioning/v1/subscribers/{ueId}), authenticated by this service's mTLS certificate. Nudr
// has no create operation for this data; see the UDR side's nfs/udr/src/oam_provisioning.hpp.
//
// Network content comes from config (network_profiles keyed by product offering id, plus
// serving PLMN, AMF field, initial SQN) -- never from literals here. K/OPc come from the order
// (the SIM vendor's input file in real operations) and are never logged or persisted by this
// service.

#include <nlohmann/json.hpp>

#include <map>
#include <sbi_core/http2_client.hpp>
#include <string>
#include <tl/expected.hpp>
#include <vector>

namespace provisioning {

struct Snssai {
    int sst = 0;
    std::string sd; // empty = no SD
};

// Per-offering network profile. Proper home is the TMF620 ProductSpecification's characteristics;
// config-keyed until the catalog carries them (disclosed, ADR-0382).
struct NetworkProfile {
    std::vector<Snssai> snssais; // first one is the default S-NSSAI
    std::string dnn;
    std::string ue_ambr_uplink;   // BitRate, e.g. "100 Mbps"
    std::string ue_ambr_downlink; // BitRate
};

struct UdrAdapterConfig {
    std::string udr_base_url;
    std::string serving_plmn_id;
    std::string authentication_method;           // AuthMethod, e.g. "5G_AKA"
    std::string authentication_management_field; // 4 hex digits
    std::string initial_sqn;                     // 12 hex digits, used when the order has none
    std::map<std::string, NetworkProfile> network_profiles; // key: offering id, or "default"
};

UdrAdapterConfig parse_udr_adapter_config(const nlohmann::json& config);

// SIM credentials from the order. Held only for the duration of one request.
struct SimCredentials {
    std::string k;   // 32 hex
    std::string opc; // 32 hex
    std::string sqn; // 12 hex or empty (config initial_sqn applies)
};

struct SubscriberSpec {
    std::string supi;
    std::string msisdn; // digits, may be empty
    std::string offering_id;
    SimCredentials sim;
};

// The profile used, returned so the saga can record it (non-secret) in the task response.
struct UdrProvisionOutcome {
    int http_status = 0;
    std::string profile_key;
};

struct UdrWriteError {
    bool no_credentials = false; // the UDR answered 409: the subscriber has no credentials
    std::string message;         // safe to log: never carries K/OPc
};

// ADR-0576, PROVISIONING_MAPPING A4: the OdbPacketServices value a barred (TMF640 "inactive")
// subscriber carries in AccessAndMobilitySubscriptionData; an unbarred one carries null.
inline constexpr const char* kBarAllPacketServices = "ALL_PACKET_SERVICES";

class UdrAdapter {
public:
    UdrAdapter(UdrAdapterConfig config, sbi_core::http2::TlsConfig tls);

    // Builds the PUT body; exposed for tests. Error if no profile applies.
    tl::expected<nlohmann::json, std::string> build_documents(const SubscriberSpec& spec,
                                                              std::string* profile_key) const;

    tl::expected<UdrProvisionOutcome, std::string> provision(const SubscriberSpec& spec);

    // ADR-0569. The network documents alone (no authenticationSubscription), with the profile
    // keyed exactly by `profile_key` -- for TMF640 that is the ServiceSpecification id (ADR-0558
    // A1); there is no "default" fallback. Exposed for tests.
    tl::expected<nlohmann::json, std::string> build_network_documents(
        const std::string& supi, const std::string& msisdn, const std::string& profile_key) const;
    bool has_profile(const std::string& profile_key) const;

    // PUT .../subscribers/{supi} without credentials. A 409 from the UDR (no credentials yet)
    // comes back as UdrWriteError::no_credentials. `barred` sets amData.odbPacketServices to
    // ALL_PACKET_SERVICES (A4); otherwise the documents carry no odbPacketServices.
    tl::expected<UdrProvisionOutcome, UdrWriteError>
    provision_network(const std::string& supi,
                      const std::string& msisdn,
                      const std::string& profile_key,
                      bool barred = false);

    // ADR-0576: PUT .../subscribers/{supi}/am-data -- the profile's amData with odbPacketServices
    // ALL_PACKET_SERVICES (barred) or null, nothing else of the subscriber rewritten.
    tl::expected<UdrProvisionOutcome, UdrWriteError>
    provision_am_data(const std::string& supi,
                      const std::string& msisdn,
                      const std::string& profile_key,
                      bool barred);

    // ADR-0576: DELETE .../subscribers/{supi}. A UDR 404 (nothing provisioned) is success, so a
    // pre-active Service and a retried delete both succeed.
    tl::expected<void, UdrWriteError> deprovision(const std::string& supi);

    // PUT .../subscribers/{supi}/authentication-subscription: the SIM keys alone.
    tl::expected<UdrProvisionOutcome, UdrWriteError>
    provision_credentials(const std::string& supi, const SimCredentials& sim);

private:
    tl::expected<UdrProvisionOutcome, UdrWriteError> put(const std::string& path,
                                                         const std::string& body);

    UdrAdapterConfig config_;
    sbi_core::http2::Client client_;
};

} // namespace provisioning
