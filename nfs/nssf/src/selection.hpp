#pragma once

// Nnssf_NSSelection_Get decision (TS 29.531 clause 5.2.2.2, TS29531_Nnssf_NSSelection.yaml),
// ADR-0640. Pure: no I/O and no store access, so tests/conformance links it on its own. main.cpp
// passes in the NSSAIAvailability records it holds.

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

#include "TS26510_CommonData_grp.hpp"

namespace nssf {

// Which slice-info-request-* query parameter the request carried (TS 29.531 5.2.2.2.2-5.2.2.2.6).
enum class SelectionProcedure {
    Registration,          // slice-info-request-for-registration (5.2.2.2.2)
    PduSession,            // slice-info-request-for-pdu-session (5.2.2.2.3)
    UeConfigurationUpdate, // slice-info-request-for-ue-cu (5.2.2.2.4)
    PdnConnection,         // slice-info-request-for-pdn-connection (5.2.2.2.5)
    OtherPurpose,          // slice-info-request-for-other-purpose (5.2.2.2.6)
    Unspecified,           // none of them: the whole catalog is asked about (pre-ADR-0640 shape)
};

struct SelectionRequest {
    SelectionProcedure procedure = SelectionProcedure::Unspecified;
    // requestedNssai, or the single sNssai (PDU session), or the S-NSSAI list (PDN/other).
    std::optional<std::vector<sbi_gen::Snssai>> requested;
    // subscribedNssai (registration, UE configuration update).
    std::optional<std::vector<sbi_gen::SubscribedSnssai>> subscribed;
    // The `tai` query parameter.
    std::optional<sbi_gen::Tai_CommonData> tai;
};

// True if `snssai` falls under one ExtSnssai (TS29571_CommonData.yaml): same sst, and the same sd,
// or `wildcardSd` true, or an sd inside one of `sdRanges` (both ends inclusive).
bool ext_snssai_matches(const nlohmann::json& ext_snssai, const sbi_gen::Snssai& snssai);

// The `supportedSnssaiList` entries that NSSAIAvailability records (each a stored
// NssaiAvailabilityInfo) report for `tai`, through `tai` or `taiList` of a
// SupportedNssaiAvailabilityData. nullopt when no record covers `tai`, which means the NSSF has no
// TA-level information for it.
std::optional<std::vector<nlohmann::json>>
ta_supported_snssais(const std::vector<nlohmann::json>& availability_infos,
                     const sbi_gen::Tai_CommonData& tai);

// The AuthorizedNetworkSliceInfo for a 200, or nullopt when no slice can be found, which the
// caller answers with 403 SNSSAI_NOT_SUPPORTED (TS 29.531 5.2.2.2.2-5.2.2.2.5 step 2b, Table
// 6.1.7.3-1). `nrf_disc_api_uri` fills NsiInformation.nrfId for a PDU session.
std::optional<sbi_gen::AuthorizedNetworkSliceInfo>
decide_slice_selection(const std::vector<sbi_gen::Snssai>& catalog,
                       const SelectionRequest& request,
                       const std::vector<nlohmann::json>& availability_infos,
                       const std::string& nrf_disc_api_uri);

} // namespace nssf
