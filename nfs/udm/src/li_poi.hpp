#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "li_core/xiri.hpp"
#include "li_poi/poi_runtime.hpp"

// The UDM's Lawful Interception IRI-POI (TS 33.127 clause 7.2.2, ADR-0475 programme). Private to
// nfs/udm. Shares the generic X1/X2 runtime (ADR-0463's libs/li-poi) with the AMF/SMF POIs; what
// stays here is UDM-specific: target matching on SUPI/GPSI/PEI, the four wired xIRI records
// (events 1-4 of ADR-0475's approved list), and the registered-target lookup for event 2.
//
// Disabled unless config/udm.json's li_poi.enabled is true; when disabled the UDM never constructs
// one, main.cpp holds a nullptr, and every report_*/note_* call below is a no-op -- the UDM behaves
// exactly as it did before the POI existed. Delivery is best-effort, the same convention as every
// other POI in this project: a dead/slow MDF2 is logged and counted, never allowed to break a UDM
// procedure (an intercept failure must not take down the network function it lives in).
//
// Wired events (TS 33.128 7.2.2.3, ADR-0475's approved list): UDMServingSystemMessage (7.2.2.3.2,
// at AMF 3GPP/non-3GPP-access registration), UDMStartOfInterceptionWithRegisteredTarget (7.2.2.3.9,
// when a task targets a SUPI that already has a registration -- disclosed: PEI/GPSI-only targets
// cannot trigger this, this UDM has no live PEI/GPSI -> SUPI resolution at hook time),
// UDMCancelLocationMessage (7.2.2.3.4, see report_cancel_location's own note on this build's one
// real trigger), UDMUEAuthenticationResponse (7.2.2.3.8, at Nudm_UEAuthentication
// generate-auth-data). Events 5 (subscriber record change) and 7 (UE information response) stay
// open per the approved list; 6 and 8 are out of scope (no HSS-IWF / ProSe).

namespace udm {

// The identities a UDM procedure has in hand, in the raw SBI ("imsi-...", "msisdn-...") form --
// converted to xIRI identity types inside li_poi.cpp, the same split every other POI in this
// project keeps (NF code stays in SBI terms, li_poi.cpp owns the xiri:: conversion).
struct LiIdentities {
    std::string supi;
    std::optional<std::string> pei;
    std::optional<std::string> gpsi;
};

class UdmLiPoi {
public:
    // Looks up the current am-data (AccessAndMobilitySubscriptionData) JSON for a bare SUPI, for
    // UDMStartOfInterceptionWithRegisteredTarget's Mandatory uDMSubscriptionDataSets field --
    // nullopt if the UDR has none for it. The event is then skipped (logged), never fabricated.
    using SubscriptionDataFetcher =
        std::function<std::optional<std::string>(const std::string& supi)>;

    UdmLiPoi(li_poi::Config config, SubscriptionDataFetcher fetch_subscription_data);
    ~UdmLiPoi();
    UdmLiPoi(const UdmLiPoi&) = delete;
    UdmLiPoi& operator=(const UdmLiPoi&) = delete;

    // Start the LI_X1 provisioning listener and the clause-6.6.2 keepalive monitor. The LI_X2
    // client connects lazily on the first emit.
    void start();
    void stop();

    // --- registered-target state for UDMStartOfInterceptionWithRegisteredTarget (ADR-0461's
    // pattern, reused here): the POI keeps its own in-process "is this SUPI registered right now"
    // map rather than reading the UDM's own registration stores directly, so it stays decoupled
    // from AmfRegistrationStore/AmfNon3GppRegistrationStore's concrete types. Disclosed: lost on
    // UDM restart, same as the AMF POI's own disclosed limitation. note_registered MUST be called
    // before report_serving_system for the same event, so a warrant activated concurrently with a
    // registration is caught by at least one of the two paths (over-report, never a miss).
    void note_registered(const std::string& supi);
    void note_deregistered(const std::string& supi);

    // TS 33.128 7.2.2.3.2: UDMServingSystemMessage when the UDM receives
    // amf3GPPAccessRegistration/amfNon3GPPAccessRegistration (Nudm_UECM_Registration, TS 29.503
    // 5.3.2.2.2/.3). No-op for a non-target identity.
    void report_serving_system(const LiIdentities& ids,
                               li_core::xiri::UdmServingSystemMethod method,
                               const std::optional<li_core::xiri::Plmnid>& plmn_id,
                               std::optional<bool> roaming_indicator);

    // TS 33.128 7.2.2.3.4: UDMCancelLocationMessage. This build's only conformant trigger is an
    // AMF 3GPP/non-3GPP-access registration Modification PATCH with purgeFlag=true (table
    // 7.2.2.3.4-1's bullets 2/3) -- the UDM-initiated DeregistrationNotification bullet (1) has no
    // real client call in this UDM and is not a gap in this call's own scope, the same "this
    // build's trigger set is a real subset of the clause's triggers" convention the AMF POI's own
    // ADR-0393 note already established for AMFDeregistration. No-op for a non-target identity.
    void report_cancel_location(const LiIdentities& ids,
                                const std::optional<li_core::xiri::Plmnid>& plmn_id,
                                li_core::xiri::UdmCancelLocationMethod method);

    // TS 33.128 7.2.2.3.8: UDMUEAuthenticationResponse at Nudm_UEAuthentication
    // generate-auth-data (TS 29.503 6.3.6.2.2) -- always called BY the AUSF per that operation's
    // own definition, so infoRequestType is always Ausf (never guessed per-call). `supi` is the
    // already-deconcealed SUPI (a SUCI on the wire never reaches this call). `serving_network_name`
    // is the TS 33.501 Annex A.1 SNN string ("5G:mnc070.mcc999.3gppnetwork.org"); if it doesn't
    // parse to that exact format the event is skipped (logged), never fabricated. No-op for a
    // non-target identity.
    void report_ue_authentication_response(const std::string& supi,
                                           const std::string& serving_network_name,
                                           li_core::xiri::PrimaryAuthenticationType auth_type,
                                           std::optional<bool> akma_indicator);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace udm
