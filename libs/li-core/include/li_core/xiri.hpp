#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <tl/expected.hpp>
#include <variant>
#include <vector>

// TS 33.128 V19.7.0 xIRI payload facade -- the BER-encoded @TS33128Payloads.XIRIPayload that fills
// an X2 PDU's Payload field with Payload Format 2 (TS 33.128 table 5.3.2-1). The ASN.1 module is
// specs/3gpp/33128-attachments/TS33128Payloads.asn (module OID ... ts33128(19) r19(19)
// version7(7)), compiled by asn1c into li_generated. This header is the ONLY way NF code reaches
// that codec: the generated C types stay inside li_core's shared object (ADR-0364 explains the
// NGAP symbol clash that forces this), so POIs describe events with the C++ structs below and
// never include a generated header.
//
// Coverage in this increment: the XIRIEvent alternatives listed in `Event`. Each struct carries
// the members its ASN.1 SEQUENCE marks non-OPTIONAL; OPTIONAL members are added when the POI that
// produces the event is built against TS 33.128's M/C/O table for it (e.g. table 6.2.2.2.2-1 for
// AMFRegistration) -- that table, not this file, decides what a conformant xIRI carries.

#pragma GCC visibility push(default)
namespace li_core::xiri {

// TS33128Payloads: AMFRegistrationType ::= ENUMERATED { initial(1) ... disasterInitial(7) }
enum class AmfRegistrationType : std::uint8_t {
    Initial = 1,
    Mobility = 2,
    Periodic = 3,
    Emergency = 4,
    SnpnOnboarding = 5,
    DisasterMobility = 6,
    DisasterInitial = 7,
};

// AMFRegistrationResult ::= ENUMERATED { threeGPPAccess(1), nonThreeGPPAccess(2),
// threeGPPAndNonThreeGPPAccess(3) }
enum class AmfRegistrationResult : std::uint8_t {
    ThreeGppAccess = 1,
    NonThreeGppAccess = 2,
    ThreeGppAndNonThreeGppAccess = 3,
};

// AMFDirection ::= ENUMERATED { networkInitiated(1), uEInitiated(2) } -- TS 33.128 6.2.2.2.3
// says whether a deregistration was network- or UE-initiated.
enum class AmfDirection : std::uint8_t {
    NetworkInitiated = 1,
    UeInitiated = 2,
};

// AccessType ::= ENUMERATED { threeGPPAccess(1), nonThreeGPPAccess(2),
// threeGPPandNonThreeGPPAccess(3) } (see TS 24.501 clause 9.11.3.20). A distinct ASN.1 type from
// AMFRegistrationResult even though the arcs coincide.
enum class AccessType : std::uint8_t {
    ThreeGppAccess = 1,
    NonThreeGppAccess = 2,
    ThreeGppAndNonThreeGppAccess = 3,
};

// IMSI ::= NumericString (SIZE(6..15)); NAI ::= UTF8String; SUPI ::= CHOICE { iMSI [1], nAI [2] }
struct Imsi {
    std::string digits;
    bool operator==(const Imsi&) const = default;
};
struct Nai {
    std::string value;
    bool operator==(const Nai&) const = default;
};
using Supi = std::variant<Imsi, Nai>;

// FiveGGUTI ::= SEQUENCE { mCC NumericString(SIZE(3)), mNC NumericString(SIZE(2..3)),
// aMFRegionID INTEGER(0..255), aMFSetID INTEGER(0..1023), aMFPointer INTEGER(0..63),
// fiveGTMSI INTEGER(0..4294967295) }
struct FiveGGuti {
    std::string mcc;
    std::string mnc;
    std::uint8_t amf_region_id = 0;
    std::uint16_t amf_set_id = 0;
    std::uint8_t amf_pointer = 0;
    std::uint32_t five_g_tmsi = 0;
    bool operator==(const FiveGGuti&) const = default;
};

// XIRIEvent.registration [1] AMFRegistration -- TS 33.128 clause 6.2.2.2. Non-OPTIONAL members.
struct AmfRegistration {
    AmfRegistrationType registration_type = AmfRegistrationType::Initial;
    AmfRegistrationResult registration_result = AmfRegistrationResult::ThreeGppAccess;
    Supi supi;
    FiveGGuti guti;
    bool operator==(const AmfRegistration&) const = default;
};

// --- TS 33.128 Location, the 5G NGAP user-location path ------------------------------------
// Location is a SEQUENCE of seven OPTIONAL branches; the AMF POI reports the NGAP-sourced form,
// Location.locationInfo.userLocation (table 6.2.2.2.4-1 form 1). Only that path is modelled here;
// positioningInfo/geoInfo/4G/IMS/coarseLocation, and the non-3GPP/UTRA/GERA/PLMN access types of
// UserLocation, are deferred (added when a POI that produces them is built). PLMNID/TAI/NCGI/ECGI
// mandatory members follow the ASN.1 exactly.

// PLMNID ::= SEQUENCE { mCC NumericString(3), mNC NumericString(2..3) }
struct Plmnid {
    std::string mcc;
    std::string mnc;
    bool operator==(const Plmnid&) const = default;
};

// TAI ::= SEQUENCE { pLMNID, tAC OCTET STRING(2..3), nID OPTIONAL }
struct Tai {
    Plmnid plmn;
    std::vector<std::uint8_t> tac; // 2..3 octets
    bool operator==(const Tai&) const = default;
};

// NCGI ::= SEQUENCE { pLMNID, nRCellID BIT STRING(36), nID OPTIONAL }
struct Ncgi {
    Plmnid plmn;
    std::uint64_t nr_cell_id = 0; // 36 bits
    bool operator==(const Ncgi&) const = default;
};

// ECGI ::= SEQUENCE { pLMNID, eUTRACellID BIT STRING(28), nID OPTIONAL }
struct Ecgi {
    Plmnid plmn;
    std::uint32_t eutra_cell_id = 0; // 28 bits
    bool operator==(const Ecgi&) const = default;
};

// NRLocation ::= SEQUENCE { tAI, nCGI, ... OPTIONAL }; only the two M members.
struct NrLocation {
    Tai tai;
    Ncgi ncgi;
    bool operator==(const NrLocation&) const = default;
};

// EUTRALocation ::= SEQUENCE { tAI, eCGI, ... OPTIONAL }; only the two M members.
struct EutraLocation {
    Tai tai;
    Ecgi ecgi;
    bool operator==(const EutraLocation&) const = default;
};

// UserLocation ::= SEQUENCE (all OPTIONAL); the two 5G access types modelled.
struct UserLocation {
    std::optional<NrLocation> nr;
    std::optional<EutraLocation> eutra;
    bool operator==(const UserLocation&) const = default;
};

// Location ::= SEQUENCE (all OPTIONAL); only locationInfo.userLocation modelled.
struct Location {
    std::optional<UserLocation> user_location;
    bool operator==(const Location&) const = default;
};

// XIRIEvent.startOfInterceptionWithRegisteredUE [4] AMFStartOfInterceptionWithRegisteredUE --
// TS 33.128 clause 6.2.2.2.5, table 6.2.2.2.5-1. Generated when LI is activated on a UE that is
// already 5GMM-REGISTERED. The M members (registrationResult, sUPI, gUTI) plus two C members the
// AMF POI has real state for (ADR-0461): location (the last known user location) and
// timeOfRegistration (REGISTRATION ACCEPT sent time, a GeneralizedTime in UTC -- the table requires
// "time zone information, i.e. as UTC or offset from UTC, not as local time", so the codec accepts
// only a string ending in 'Z'). registrationType/slice and the rest stay C/O, not modelled.
struct AmfStartOfInterceptionWithRegisteredUE {
    AmfRegistrationResult registration_result = AmfRegistrationResult::ThreeGppAccess;
    Supi supi;
    FiveGGuti guti;
    std::optional<Location> location;
    std::optional<std::string> time_of_registration; // "YYYYMMDDHHMMSSZ", UTC
    bool operator==(const AmfStartOfInterceptionWithRegisteredUE&) const = default;
};

// XIRIEvent.deregistration [2] AMFDeregistration -- TS 33.128 clause 6.2.2.2.3, table
// 6.2.2.2.3-1. deregistration_direction/access_type are the two M members. sUPI/gUTI/location are
// C ("if available") -- ADR-0393 wires them, since the AMF POI now has real state to populate
// them from. sUCI/pEI/gPSI/cause/reRegRequiredIndicator/unavailabilityPeriodDuration/
// additionalUserIdentifiers remain unpopulated C/O members: this project's AMF has no SUCI
// retained past registration, no PEI capture, no GPSI mapping, and this increment's scope is a
// UE-originating ACCEPT (no reject cause applies) -- disclosed, not silently dropped.
struct AmfDeregistration {
    AmfDirection deregistration_direction = AmfDirection::NetworkInitiated;
    AccessType access_type = AccessType::ThreeGppAccess;
    std::optional<Supi> supi;
    std::optional<FiveGGuti> guti;
    std::optional<Location> location;
    // SwitchOffIndicator ::= ENUMERATED { normalDetach(1), switchOff(2) } -- TS 24.501 §5.5.2.2's
    // "switch off" deregistration-type bit, real and cheaply available since this POI's trigger
    // decodes that exact bit to decide whether to send DEREGISTRATION ACCEPT at all (see
    // TS 33.128 6.2.2.2.3's own two UE-initiated bullets).
    enum class SwitchOff : std::uint8_t { NormalDetach = 1, SwitchOff = 2 };
    std::optional<SwitchOff> switch_off_indicator;
    bool operator==(const AmfDeregistration&) const = default;
};

// XIRIEvent.locationUpdate [3] AMFLocationUpdate -- TS 33.128 clause 6.2.2.2.4, table
// 6.2.2.2.4-1. M members: sUPI and location; every other member is C/O.
struct AmfLocationUpdate {
    Supi supi;
    Location location;
    bool operator==(const AmfLocationUpdate&) const = default;
};

// XIRIEvent.aMFIdentifierAssociation [62] AMFIdentifierAssociation -- TS 33.128 clause 6.2.2.2.7,
// table 6.2.2.2.7-1. M members: sUPI, gUTI, location. Generated only when identifier-association
// reporting is enabled for the target (clause 6.2.2.1); that gating lives at the POI, not here.
struct AmfIdentifierAssociation {
    Supi supi;
    FiveGGuti guti;
    Location location;
    bool operator==(const AmfIdentifierAssociation&) const = default;
};

// XIRIEvent.aMFIdentifierDeassociation [186] AMFIdentifierDeassociation -- table 6.2.2.2.7-2.
// M members: sUPI, gUTI; location is OPTIONAL here (unlike association).
struct AmfIdentifierDeassociation {
    Supi supi;
    FiveGGuti guti;
    std::optional<Location> location;
    bool operator==(const AmfIdentifierDeassociation&) const = default;
};

// --- SMF xIRI records, TS 33.128 clause 6.2.3.2 (ADR-0463) ---------------------------------------
// The facade models the mandatory members of each record plus the optional ones an SMF POI has real
// state for (identities, slice, UE address, location, access type). MA PDU sessions, ProSe, EPS
// interworking, PCC rules and the other C/O members are not modelled (disclosed).

// PDUSessionType ::= ENUMERATED { iPv4(1), iPv6(2), iPv4v6(3), unstructured(4), ethernet(5) }
enum class PduSessionType : std::uint8_t {
    IPv4 = 1,
    IPv6 = 2,
    IPv4v6 = 3,
    Unstructured = 4,
    Ethernet = 5
};

// FiveGSMRequestType ::= ENUMERATED (reserved(6) is not modelled)
enum class SmRequestType : std::uint8_t {
    InitialRequest = 1,
    ExistingPduSession = 2,
    InitialEmergencyRequest = 3,
    ExistingEmergencyPduSession = 4,
    ModificationRequest = 5,
    MaPduRequest = 7,
};

// Initiator ::= ENUMERATED { uE(1), network(2), unknown(3) }
enum class SmInitiator : std::uint8_t { Ue = 1, Network = 2, Unknown = 3 };

// SMFFailedProcedureType ::= ENUMERATED { pDUSessionEstablishment(1), ...Modification(2),
// ...Release(3) }
enum class SmFailedProcedure : std::uint8_t {
    PduSessionEstablishment = 1,
    PduSessionModification = 2,
    PduSessionRelease = 3
};

// FTEID ::= SEQUENCE { tEID, iPv4Address OPTIONAL, iPv6Address OPTIONAL }
struct Fteid {
    std::uint32_t teid = 0;
    std::optional<std::array<std::uint8_t, 4>> ipv4;
    std::optional<std::array<std::uint8_t, 16>> ipv6;
    bool operator==(const Fteid&) const = default;
};

// SNSSAI ::= SEQUENCE { sliceServiceType, sliceDifferentiator OPTIONAL, mapped... } -- the home
// mapped values are not modelled.
struct Snssai {
    std::uint8_t sst = 0;
    std::optional<std::array<std::uint8_t, 3>> sd;
    bool operator==(const Snssai&) const = default;
};

// UEEndpointAddress ::= CHOICE { iPv4Address, iPv6Address, ethernetAddress (MAC) }
using UeEndpoint = std::
    variant<std::array<std::uint8_t, 4>, std::array<std::uint8_t, 16>, std::array<std::uint8_t, 6>>;

// PEI ::= CHOICE { iMEI, iMEISV, mACAddress, eUI64 } -- IMEI (14 digits) and IMEISV (16 digits)
// only.
struct Imei {
    std::string digits;
    bool operator==(const Imei&) const = default;
};
struct Imeisv {
    std::string digits;
    bool operator==(const Imeisv&) const = default;
};
using Pei = std::variant<Imei, Imeisv>;

// GPSI ::= CHOICE { mSISDN, nAI }
struct Msisdn {
    std::string digits;
    bool operator==(const Msisdn&) const = default;
};
using Gpsi = std::variant<Msisdn, Nai>;

// The identities an SMF xIRI may carry; SUPI, PEI and GPSI are independent (TS 33.127 6.2.3.2).
struct SmIdentities {
    std::optional<Supi> supi;
    std::optional<Pei> pei;
    std::optional<Gpsi> gpsi;
    bool operator==(const SmIdentities&) const = default;
};

// XIRIEvent.pDUSessionEstablishment [6] SMFPDUSessionEstablishment, table 6.2.3.2.2-1. M:
// pDUSessionID, gTPTunnelID, pDUSessionType, dNN, requestType.
struct SmfPduSessionEstablishment {
    SmIdentities ids;
    std::uint8_t pdu_session_id = 0;
    Fteid gtp_tunnel; // the UPF N3 uplink F-TEID the session was established on
    PduSessionType pdu_session_type = PduSessionType::IPv4;
    std::optional<Snssai> snssai;
    std::vector<UeEndpoint> ue_endpoints;
    std::optional<Location> location;
    std::string dnn;
    SmRequestType request_type = SmRequestType::InitialRequest;
    std::optional<AccessType> access_type;
    bool operator==(const SmfPduSessionEstablishment&) const = default;
};

// XIRIEvent.pDUSessionModification [7] SMFPDUSessionModification, table 6.2.3.2.3-1. M:
// requestType.
struct SmfPduSessionModification {
    SmIdentities ids;
    std::optional<Snssai> snssai;
    std::optional<Location> location;
    SmRequestType request_type = SmRequestType::ModificationRequest;
    std::optional<AccessType> access_type;
    std::optional<std::uint8_t> pdu_session_id;
    std::optional<UeEndpoint> ue_endpoint;
    bool operator==(const SmfPduSessionModification&) const = default;
};

// XIRIEvent.pDUSessionRelease [8] SMFPDUSessionRelease, table 6.2.3.2.4-1. M: sUPI, pDUSessionID.
struct SmfPduSessionRelease {
    Supi supi;
    std::optional<Pei> pei;
    std::optional<Gpsi> gpsi;
    std::uint8_t pdu_session_id = 0;
    std::optional<Location> location;
    bool operator==(const SmfPduSessionRelease&) const = default;
};

// XIRIEvent.startOfInterceptionWithEstablishedPDUSession [9], table 6.2.3.2.5-1. M: pDUSessionID,
// gTPTunnelID, pDUSessionType, uEEndpoint, dNN, requestType.
struct SmfStartOfInterceptionWithEstablishedPduSession {
    SmIdentities ids;
    std::uint8_t pdu_session_id = 0;
    Fteid gtp_tunnel;
    PduSessionType pdu_session_type = PduSessionType::IPv4;
    std::optional<Snssai> snssai;
    std::vector<UeEndpoint> ue_endpoints;
    std::optional<Location> location;
    std::string dnn;
    SmRequestType request_type = SmRequestType::InitialRequest;
    std::optional<AccessType> access_type;
    bool operator==(const SmfStartOfInterceptionWithEstablishedPduSession&) const = default;
};

// XIRIEvent.unsuccessfulSMProcedure [10] SMFUnsuccessfulProcedure, table 6.2.3.2.6-1. M:
// failedProcedureType, failureCause (a 5GSM cause, TS 24.501 9.11.4.2), initiator.
struct SmfUnsuccessfulProcedure {
    SmFailedProcedure failed_procedure = SmFailedProcedure::PduSessionEstablishment;
    std::uint8_t failure_cause = 0;
    SmInitiator initiator = SmInitiator::Unknown;
    SmIdentities ids;
    std::optional<std::uint8_t> pdu_session_id;
    std::vector<UeEndpoint> ue_endpoints;
    std::optional<std::string> dnn;
    std::optional<SmRequestType> request_type;
    std::optional<AccessType> access_type;
    std::optional<Location> location;
    bool operator==(const SmfUnsuccessfulProcedure&) const = default;
};

// --- UDM xIRI records, TS 33.128 clause 7.2.2.3 (ADR-0475, user-approved event list 2026-10-09)
// --- XIRIEvent.servingSystemMessage [11] UDMServingSystemMessage -- table of clause 7.2.2.3 for
// the serving-system event. M members: sUPI, servingSystemMethod. Modelled C/O members: pEI, gPSI,
// pLMNID, roamingIndicator. NOT modelled (disclosed): gUAMI, gUMMEI, serviceID.
// UDMServingSystemMethod ::= ENUMERATED { amf3GPPAccessRegistration(0),
//                                         amfNon3GPPAccessRegistration(1), unknown(2) }
enum class UdmServingSystemMethod : std::uint8_t {
    Amf3GppAccessRegistration = 0,
    AmfNon3GppAccessRegistration = 1,
    Unknown = 2
};

struct UdmServingSystemMessage {
    Supi supi;
    std::optional<Pei> pei;
    std::optional<Gpsi> gpsi;
    std::optional<Plmnid> plmn_id;
    UdmServingSystemMethod serving_system_method = UdmServingSystemMethod::Unknown;
    std::optional<bool> roaming_indicator;
    bool operator==(const UdmServingSystemMessage&) const = default;
};

// XIRIEvent.uDMStartOfInterceptionWithRegisteredTarget [124]
// UDMStartOfInterceptionWithRegisteredTarget (TS 33.128 clause 7.2.2.3): sent when interception
// starts for a target the UDM already holds registration data for. M members: sUPI,
// uDMSubscriptionDataSets (an SBIType: a reference string naming the SBI resource and its value,
// both UTF8String). gPSI is optional. The SBI reference/value strings are carried verbatim; WHICH
// subscription data sets and how they are rendered (reference naming, JSON value) is a UDM-POI
// decision not yet made (disclosed).
struct UdmStartOfInterceptionWithRegisteredTarget {
    Supi supi;
    std::optional<Gpsi> gpsi;
    std::string sbi_reference;
    std::string sbi_value;
    bool operator==(const UdmStartOfInterceptionWithRegisteredTarget&) const = default;
};

// XIRIEvent.cancelLocationMessage [55] UDMCancelLocationMessage (TS 33.128 clause 7.2.2.3.4). M
// members: sUPI, cancelLocationMethod. Modelled C members: pEI, gPSI, pLMNID. NOT modelled
// (disclosed, same complexity-skip precedent as UdmServingSystemMessage's gUAMI/gUMMEI/serviceID):
// gUAMI, aMFDeregistrationInfo, deregistrationData.
// UDMCancelLocationMethod ::= ENUMERATED { aMF3GPPAccessDeregistration(1),
//                                          aMFNon3GPPAccessDeregistration(2), uDMDeregistration(3),
//                                          unknown(4) }
enum class UdmCancelLocationMethod : std::uint8_t {
    Amf3GppAccessDeregistration = 1,
    AmfNon3GppAccessDeregistration = 2,
    UdmDeregistration = 3,
    Unknown = 4
};

struct UdmCancelLocationMessage {
    Supi supi;
    std::optional<Pei> pei;
    std::optional<Gpsi> gpsi;
    std::optional<Plmnid> plmn_id;
    UdmCancelLocationMethod cancel_location_method = UdmCancelLocationMethod::Unknown;
    bool operator==(const UdmCancelLocationMessage&) const = default;
};

// XIRIEvent.uDMUEAuthenticationResponse [110] UDMUEAuthenticationResponse (TS 33.128 clause
// 7.2.2.3.8). M members: sUPI, authenticationInfoRequest. Modelled C members: aKMAIndicator,
// authAAA. NOT modelled (disclosed): problemDetails, pvsInfo -- a scope cut, not a security-driven
// omission. Tables 7.2.2.3.8-1 and -2 were read in full before coding this event (ADR-0475's own
// required gate): neither table carries key material (no K, OPc, or RAND-derived secrets).
// UDMInfoRequestType ::= ENUMERATED { hSS(1), aUSF(2), other(3) }
enum class UdmInfoRequestType : std::uint8_t { Hss = 1, Ausf = 2, Other = 3 };

// PrimaryAuthenticationType ::= ENUMERATED { eAPAKAPrime(1), fiveGAKA(2), eAPTLS(3), none(4),
//                                            ePSAKA(5), eAPAKA(6), iMSAKA(7), gBAAKA(8), uMTSAKA(9)
//                                            }
enum class PrimaryAuthenticationType : std::uint8_t {
    EapAkaPrime = 1,
    FiveGAka = 2,
    EapTls = 3,
    None = 4,
    EpsAka = 5,
    EapAka = 6,
    ImsAka = 7,
    GbaAka = 8,
    UmtsAka = 9
};

// UDMAuthenticationInfoRequest ::= SEQUENCE { infoRequestType [1], rGAuthCtx [2] SEQUENCE
// SIZE(1..MAX) OF SubscriberIdentifier, authType [3], servingNetworkName [4] PLMNID,
// aUSFInstanceID [5] OPTIONAL, cellCAGInfo [6] OPTIONAL, n5GCIndicator [7] OPTIONAL }. rGAuthCtx is
// Mandatory, SIZE(1..MAX), CHOICE { sUCI, sUPI } per entry -- only the sUPI alternative is modelled
// (disclosed): fill() requires >=1 entry, and a received sUCI-typed entry is a decode error, never
// silently dropped. NOT modelled (disclosed): aUSFInstanceID, cellCAGInfo, n5GCIndicator.
struct UdmAuthenticationInfoRequest {
    UdmInfoRequestType info_request_type = UdmInfoRequestType::Other;
    std::vector<Supi> rg_auth_ctx_supis;
    PrimaryAuthenticationType auth_type = PrimaryAuthenticationType::None;
    Plmnid serving_network_name;
    bool operator==(const UdmAuthenticationInfoRequest&) const = default;
};

struct UdmUeAuthenticationResponse {
    Supi supi;
    UdmAuthenticationInfoRequest authentication_info_request;
    std::optional<bool> akma_indicator;
    std::optional<bool> auth_aaa;
    bool operator==(const UdmUeAuthenticationResponse&) const = default;
};

// ---- SMSF (TS 33.128 clause 6.2.5.3, ADR-0486) ----

// SMSParty ::= SEQUENCE { sUPI, pEI, gPSI, sMSAddress } -- all OPTIONAL. sMSAddress holds the raw
// TS 23.040 clause 9.1.2.5 address field octets (NOTE 2/3 of table 6.2.5.3-1), SIZE(2..12).
struct SmsParty {
    std::optional<Supi> supi;
    std::optional<Pei> pei;
    std::optional<Gpsi> gpsi;
    std::optional<std::vector<std::uint8_t>> sms_address;
    bool operator==(const SmsParty&) const = default;
};

// Direction ::= ENUMERATED { fromTarget(1), toTarget(2) }
enum class SmsDirection : std::uint8_t { FromTarget = 1, ToTarget = 2 };

// SMSTransferStatus ::= ENUMERATED { transferSucceeded(1), transferFailed(2), undefined(3) }
enum class SmsTransferStatus : std::uint8_t {
    TransferSucceeded = 1,
    TransferFailed = 2,
    Undefined = 3
};

// SMSMessageType ::= ENUMERATED -- table 6.2.5.3-4 maps (RP MTI, TP-MTI) onto these.
enum class SmsMessageType : std::uint8_t {
    Deliver = 1,
    DeliverReportAck = 2,
    DeliverReportError = 3,
    StatusReport = 4,
    Command = 5,
    Submit = 6,
    SubmitReportAck = 7,
    SubmitReportError = 8,
    Reserved = 9
};

// SMSNFType ::= ENUMERATED { sMSGMSC(1), iWMSC(2), sMSRouter(3) }
enum class SmsNfType : std::uint8_t { SmsGmsc = 1, Iwmsc = 2, SmsRouter = 3 };

// SMSTPDUData ::= CHOICE -- only the two 3GPP choices table 6.2.5.3-3 says to use are modelled:
// sMSTPDU [1] and redactedSMSTPDU [3] (both SMSTPDU, SIZE(1..270)). truncatedSMSTPDU ("superseded,
// backwards compatibility only") and the two 3GPP2 choices are not (disclosed).
struct SmsTpduData {
    bool redacted = false;
    std::vector<std::uint8_t> tpdu;
    bool operator==(const SmsTpduData&) const = default;
};

// XIRIEvent.sMSMessage [12] SMSMessage (table 6.2.5.3-1). NOT modelled (disclosed): peerNFAddress
// -- this project has no SMS-GMSC/IWMSC/SMS-Router, so there is never one to report.
struct SmsMessage {
    SmsParty originating_party;
    SmsParty terminating_party;
    SmsDirection direction = SmsDirection::FromTarget;
    SmsTransferStatus link_transfer_status = SmsTransferStatus::Undefined;
    std::optional<bool> other_message;
    std::optional<Location> location;
    std::optional<SmsNfType> peer_nf_type;
    std::optional<SmsTpduData> tpdu_data;
    std::optional<SmsMessageType> message_type;
    std::optional<std::uint8_t> rp_message_reference;
    bool operator==(const SmsMessage&) const = default;
};

// XIRIEvent.sMSReport [56] SMSReport (table 6.2.5.3-2).
struct SmsReport {
    std::optional<Location> location;
    SmsTpduData tpdu_data;
    SmsMessageType message_type = SmsMessageType::Reserved;
    std::uint8_t rp_message_reference = 0;
    bool operator==(const SmsReport&) const = default;
};

// SBIType ::= SEQUENCE { sBIReference UTF8String, sBIValue UTF8String } -- an SBI resource carried
// as JSON text, with the "<yaml file>#/components/schemas/<name>" it conforms to.
struct SbiType {
    std::string reference;
    std::string value;
    bool operator==(const SbiType&) const = default;
};

// AForASSessionWithQoSOpType ::= ENUMERATED { pOST(1), pUT(2), pATCH(3), dELETE(4) }
enum class AfSessionWithQosOpType : std::uint8_t { Post = 1, Put = 2, Patch = 3, Delete = 4 };

// AForASSessionWithQoSResponseCode ::= ENUMERATED -- only these HTTP statuses have a value; any
// other status has no encoding (to_af_session_response_code returns nullopt).
enum class AfSessionWithQosResponseCode : std::uint8_t {
    Ok200 = 1,
    Created201 = 2,
    NoContent204 = 3,
    TemporaryRedirect307 = 4,
    PermanentRedirect308 = 5,
    BadRequest400 = 6,
    Unauthorized401 = 7,
    Forbidden403 = 8,
    NotFound404 = 9,
    NotAcceptable406 = 10,
    LengthRequired411 = 11,
    UnsupportedMediaType415 = 12,
    TooManyRequests429 = 13,
    InternalServerError500 = 14,
    ServiceUnavailable503 = 15
};
std::optional<AfSessionWithQosResponseCode> to_af_session_response_code(int http_status);

// XIRIEvent.nEFAFSessionWithQoSProvision [135] (TS 33.128 table 7.7.6.1.2-1).
struct NefAfSessionWithQosProvision {
    Gpsi gpsi;
    std::string af_id;
    AfSessionWithQosOpType op_type = AfSessionWithQosOpType::Post;
    std::optional<SbiType> subscription;       // POST / PUT
    std::optional<SbiType> subscription_patch; // PATCH
    AfSessionWithQosResponseCode response_code = AfSessionWithQosResponseCode::Ok200;
    bool operator==(const NefAfSessionWithQosProvision&) const = default;
};

// NchfChargingEvent ::= ENUMERATED (TS 33.128 table 7.19.3.7-1).
enum class NchfChargingEvent : std::uint8_t {
    PduSessionChargingInformation = 1,
    SmsChargingInformation = 2,
    NefChargingInformation = 3,
    RegistrationChargingInformation = 4,
    N2ConnectionChargingInformation = 5,
    LocationReportingChargingInformation = 6,
    NspaChargingInformation = 7,
    NsmChargingInformation = 8,
    MmTelChargingInformation = 9,
    ImsChargingInformation = 10,
    EdgeInfrastructureUsageChargingInformation = 11,
    EasDeploymentChargingInformation = 12,
    DirectEdgeEnablingServiceChargingInformation = 13,
    ExposedEdgeEnablingServiceChargingInformation = 14,
    ProSeChargingInformation = 15,
    MmsChargingInformation = 16,
    MbsSessionChargingInformation = 17,
    TsnChargingInformation = 18,
    NsacfChargingInformation = 19,
    NssaaChargingInformation = 20,
    RangingSlChargingInformation = 21
};

// RoamerInOut ::= ENUMERATED { in-bound(1), out-bound(2) }
enum class RoamerInOut : std::uint8_t { InBound = 1, OutBound = 2 };

// RfChargingEvent ::= ENUMERATED (TS 33.128 table 7.19.3.12-1).
enum class RfChargingEvent : std::uint8_t {
    AocInformation = 1,
    PsInformation = 2,
    ImsInformation = 3,
    MmsInformation = 4,
    LcsInformation = 5,
    PocInformation = 6,
    MbmsInformation = 7,
    SmsInformation = 8,
    VcsInformation = 9,
    MmTelInformation = 10,
    ProSeInformation = 11,
    ServiceGenericInformation = 12,
    ImInformation = 13,
    DcdInformation = 14,
    M2mInformation = 15,
    CpdtInformation = 16
};

// ChargingDataInformation.nchfChargingDataRequest (table 7.19.3.4-1). roamer_in_out is MANDATORY in
// the j70 ASN.1 although the table says 0..1 -- see ADR-0488 for why it is never defaulted.
struct NchfChargingDataRequest {
    // fullSBIChargingData: encapsulatedSBIOfflineChargingData when true, else
    // encapsulatedSBIConvergedChargingData.
    bool offline = false;
    SbiType charging_data;
    std::optional<NchfChargingEvent> charging_event;
    RoamerInOut roamer_in_out = RoamerInOut::InBound;
    bool operator==(const NchfChargingDataRequest&) const = default;
};

// ChargingDataInformation.rfChargingDataRequest (table 7.19.3.9-1): the ACR's octets as received.
struct RfChargingDataRequest {
    std::vector<std::uint8_t> acr;
    std::optional<RfChargingEvent> charging_event;
    bool operator==(const RfChargingDataRequest&) const = default;
};

// XIRIEvent.chargingDataEvent [190] ChargingDataEvent (TS 33.128 table 7.19.3.2-1). The
// translatedChargingDataInfo choice is not modelled (ADR-0488).
struct ChargingDataEvent {
    std::optional<Location> location;
    std::variant<NchfChargingDataRequest, RfChargingDataRequest> information;
    bool operator==(const ChargingDataEvent&) const = default;
};

using Event = std::variant<AmfRegistration,
                           AmfDeregistration,
                           AmfStartOfInterceptionWithRegisteredUE,
                           AmfLocationUpdate,
                           AmfIdentifierAssociation,
                           AmfIdentifierDeassociation,
                           SmfPduSessionEstablishment,
                           SmfPduSessionModification,
                           SmfPduSessionRelease,
                           SmfStartOfInterceptionWithEstablishedPduSession,
                           SmfUnsuccessfulProcedure,
                           UdmServingSystemMessage,
                           UdmStartOfInterceptionWithRegisteredTarget,
                           UdmCancelLocationMessage,
                           UdmUeAuthenticationResponse,
                           SmsMessage,
                           SmsReport,
                           NefAfSessionWithQosProvision,
                           ChargingDataEvent>;

// BER-encodes XIRIPayload { xIRIPayloadOID = {4 19 19 7 1}, event }. The OID is the module's own
// xIRIPayloadOID (tS33128PayloadsOID xIRI(1)) -- TS 33.128 table 5.3.2-3: "the value of the
// xIRIPayloadOID specified in the version of the ASN.1 used by the IRI-POI". DER is used for the
// encoder side (a valid BER encoding, canonical); the decoder accepts any BER.
tl::expected<std::vector<std::uint8_t>, std::string> encode_xiri_payload(const Event& event);

struct DecodedXiri {
    std::vector<std::uint32_t> payload_oid; // relative OID arcs as received
    Event event;
};
// Decodes a BER XIRIPayload. Events outside `Event` decode structurally but are reported as an
// error naming the CHOICE alternative -- the MDF2 will widen this as POIs are added.
tl::expected<DecodedXiri, std::string> decode_xiri_payload(std::span<const std::uint8_t> bytes);

// The arcs encode_xiri_payload() writes, for tests and the MDF2's version check.
inline constexpr std::uint32_t kXiriPayloadOidArcs[] = {4, 19, 19, 7, 1};

} // namespace li_core::xiri
#pragma GCC visibility pop
