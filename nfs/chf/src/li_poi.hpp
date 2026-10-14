#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "diameter_core/avp.hpp"
#include "li_poi/poi_runtime.hpp"

// The charging function's Lawful Interception IRI-POI (TS 33.127 clause 7.22, TS 33.128 clause
// 7.19, ADR-0488). Private to nfs/chf; shares the generic X1/X2 runtime (libs/li-poi).
//
// Event: the CHF receives a ChargingDataRequest over Nchf (Nchf_ConvergedCharging or
// Nchf_OfflineOnlyCharging Create / Update / Release) for a target -> ChargingDataEvent with the
// nchfChargingDataRequest choice: the request body unmodified as SBIType, nchfChargingEvent from
// the service-specific charging information present, location from its userLocationinfo.
//
// Generated only for a task whose LI_X1 ActivateTask carried ChargingDataEvents (table 7.19.1-1):
// - DefaultChargingDataInformation: as above.
// - TranslatedChargingDataInformation: NOT supported (TranslatedChargingDataInfo is not built) --
//   no record, logged, disclosed.
//
// roamerInOut (ADR-0488, user decision B): TS 33.128 j70's ASN.1 makes it mandatory, table
// 7.19.3.4-1 makes it 0..1. The ASN.1 is followed strictly: a record is generated only when the
// service-specific UserInformation carries roamerInOut; otherwise it is withheld and logged. A
// home-network subscriber therefore produces no ChargingDataEvent today (nfs/smf sends none).
//
// Targets (clause 7.19.1): SUPIIMSI, SUPINAI, IMSI, GPSIMSISDN, GPSINAI, E164Number (MSISDN),
// PEIIMEI, PEIIMEISV, IMEI -- from subscriberIdentifier and UserInformation.servedGPSI/servedPEI.
// Rf (clause 7.19.3.9): every Accounting-Request the CHF's Diameter server receives, matched on
// User-Name and Service-Information/Subscription-Id -> ChargingDataEvent(rfChargingDataRequest)
// with the ACR octets and rfChargingEvent; no roamerInOut member, so no ADR-0488 withholding
// applies. Location for Rf (binary 3GPP-User-Location-Info) is not decoded -- omitted, disclosed.
// Not supported (disclosed): SIP-URI and IMEISV targets (no X1 kind modelled for them here). A
// retransmitted request is reported again (it is received again).

namespace chf {

class ChfLiPoi {
public:
    explicit ChfLiPoi(li_poi::Config config);
    ~ChfLiPoi();
    ChfLiPoi(const ChfLiPoi&) = delete;
    ChfLiPoi& operator=(const ChfLiPoi&) = delete;

    void start();
    void stop();

    // `body` is the ChargingDataRequest exactly as received; `offline` selects
    // encapsulatedSBIOfflineChargingData (Nchf_OfflineOnlyCharging) over the converged choice.
    void report_charging_data_request(const std::string& body, bool offline);

    // TS 33.128 clause 7.19.3.9: an Rf Accounting-Request as received (`message` = header + AVPs),
    // -> ChargingDataEvent with the rfChargingDataRequest choice.
    void report_rf_acr(const std::vector<std::uint8_t>& message,
                       const std::vector<diameter_core::Avp>& avps);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chf
