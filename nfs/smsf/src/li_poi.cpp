#include "li_poi.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>

#include "li_core/xiri.hpp"

namespace smsf {

namespace xiri = li_core::xiri;
namespace x1 = li_core::x1;
using nlohmann::json;

namespace {

// Same SBI-identity-scheme stripper every POI in this project carries.
std::string bare_identity(const std::string& s) {
    for (const char* prefix : {"imsi-", "nai-", "supi-", "imei-", "imeisv-", "msisdn-", "extid-"}) {
        const std::size_t len = std::char_traits<char>::length(prefix);
        if (s.size() >= len && s.compare(0, len, prefix) == 0) {
            return s.substr(len);
        }
    }
    return s;
}

bool starts_with(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

// TS 29.571 "imei-<15 digits>" carries the check digit; the xIRI IMEI (SIZE(14)) does not.
std::string pei_for_matching(const std::string& pei) {
    std::string bare = bare_identity(pei);
    if (starts_with(pei, "imei-") && bare.size() == 15) {
        bare.pop_back();
    }
    return bare;
}

using K = x1::TargetIdentifierKind;
const std::vector<K> kSupiKinds{K::SupiImsi, K::SupiNai};
const std::vector<K> kPeiKinds{K::PeiImei, K::PeiImeisv};
const std::vector<K> kGpsiKinds{K::GpsiMsisdn, K::GpsiNai};
// TS 33.128 clause 6.2.5.1: SUPIIMSI, SUPINAI, PEIIMEI, PEIIMEISV, GPSIMSISDN, GPSINAI.
const std::vector<K> kSupportedKinds{
    K::SupiImsi, K::SupiNai, K::PeiImei, K::PeiImeisv, K::GpsiMsisdn, K::GpsiNai};

xiri::Supi to_xiri_supi(const std::string& supi) {
    const std::string bare = bare_identity(supi);
    return starts_with(supi, "nai-") ? xiri::Supi{xiri::Nai{bare}} : xiri::Supi{xiri::Imsi{bare}};
}

std::optional<xiri::Pei> to_xiri_pei(const std::optional<std::string>& pei) {
    if (!pei) {
        return std::nullopt;
    }
    const std::string v = pei_for_matching(*pei);
    if (v.size() == 16) {
        return xiri::Pei{xiri::Imeisv{v}};
    }
    if (v.size() == 14) {
        return xiri::Pei{xiri::Imei{v}};
    }
    return std::nullopt;
}

std::optional<xiri::Gpsi> to_xiri_gpsi(const std::optional<std::string>& gpsi) {
    if (!gpsi) {
        return std::nullopt;
    }
    const std::string bare = bare_identity(*gpsi);
    return starts_with(*gpsi, "msisdn-") ? xiri::Gpsi{xiri::Msisdn{bare}}
                                         : xiri::Gpsi{xiri::Nai{bare}};
}

// ---- TS 29.571 UserLocation JSON -> xIRI Location (NOTE 7: Location.locationInfo.userLocation) --

std::optional<xiri::Plmnid> plmn_from(const json& j) {
    if (!j.is_object() || !j.contains("mcc") || !j.contains("mnc")) {
        return std::nullopt;
    }
    return xiri::Plmnid{j.at("mcc").get<std::string>(), j.at("mnc").get<std::string>()};
}

std::optional<std::vector<std::uint8_t>> hex_octets(const std::string& hex) {
    if (hex.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        try {
            out.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return out;
}

// TS 29.571 Tai: plmnId + tac (4 or 6 hex digits).
std::optional<xiri::Tai> tai_from(const json& j) {
    if (!j.is_object() || !j.contains("plmnId") || !j.contains("tac")) {
        return std::nullopt;
    }
    auto plmn = plmn_from(j.at("plmnId"));
    auto tac = hex_octets(j.at("tac").get<std::string>());
    if (!plmn || !tac || (tac->size() != 2 && tac->size() != 3)) {
        return std::nullopt;
    }
    return xiri::Tai{*plmn, *tac};
}

std::optional<xiri::Location> location_from(const json& ul) {
    try {
        xiri::UserLocation out;
        if (ul.contains("nrLocation")) {
            const auto& nr = ul.at("nrLocation");
            auto tai = tai_from(nr.at("tai"));
            auto plmn = plmn_from(nr.at("ncgi").at("plmnId"));
            const auto cell = nr.at("ncgi").at("nrCellId").get<std::string>(); // 9 hex digits
            if (tai && plmn && cell.size() == 9) {
                out.nr = xiri::NrLocation{*tai, xiri::Ncgi{*plmn, std::stoull(cell, nullptr, 16)}};
            }
        }
        if (ul.contains("eutraLocation")) {
            const auto& eu = ul.at("eutraLocation");
            auto tai = tai_from(eu.at("tai"));
            auto plmn = plmn_from(eu.at("ecgi").at("plmnId"));
            const auto cell = eu.at("ecgi").at("eutraCellId").get<std::string>(); // 7 hex digits
            if (tai && plmn && cell.size() == 7) {
                out.eutra = xiri::EutraLocation{
                    *tai,
                    xiri::Ecgi{*plmn, static_cast<std::uint32_t>(std::stoul(cell, nullptr, 16))}};
            }
        }
        if (!out.nr && !out.eutra) {
            return std::nullopt;
        }
        return xiri::Location{out};
    } catch (const std::exception&) {
        return std::nullopt; // malformed location: omitted (C field), never guessed
    }
}

// TS 33.128 table 6.2.5.3-4: (RP MTI, TP-MTI) -> SMSMessageType.
std::optional<xiri::SmsMessageType> message_type(sms_core::RpMti rp, sms_core::TpduType tp) {
    using sms_core::RpMti;
    using sms_core::TpduType;
    if (rp == RpMti::DataMsToNetwork && tp == TpduType::Submit) {
        return xiri::SmsMessageType::Submit;
    }
    if (rp == RpMti::DataMsToNetwork && tp == TpduType::Command) {
        return xiri::SmsMessageType::Command;
    }
    if (rp == RpMti::AckNetworkToMs && tp == TpduType::SubmitReport) {
        return xiri::SmsMessageType::SubmitReportAck;
    }
    if (rp == RpMti::ErrorNetworkToMs && tp == TpduType::SubmitReport) {
        return xiri::SmsMessageType::SubmitReportError;
    }
    if (rp == RpMti::DataNetworkToMs && tp == TpduType::Deliver) {
        return xiri::SmsMessageType::Deliver;
    }
    if (rp == RpMti::DataNetworkToMs && tp == TpduType::StatusReport) {
        return xiri::SmsMessageType::StatusReport;
    }
    if (rp == RpMti::AckMsToNetwork && tp == TpduType::DeliverReport) {
        return xiri::SmsMessageType::DeliverReportAck;
    }
    if (rp == RpMti::ErrorMsToNetwork && tp == TpduType::DeliverReport) {
        return xiri::SmsMessageType::DeliverReportError;
    }
    return std::nullopt;
}

// The TP-MTI is direction-dependent (TS 23.040 clause 9.2.3.1); the RP MTI says which way.
sms_core::Direction tpdu_direction(sms_core::RpMti rp) {
    using sms_core::RpMti;
    switch (rp) {
        case RpMti::DataMsToNetwork:
        case RpMti::AckMsToNetwork:
        case RpMti::ErrorMsToNetwork:
        case RpMti::SmmaMsToNetwork:
            return sms_core::Direction::MsToSc;
        default:
            return sms_core::Direction::ScToMs;
    }
}

} // namespace

struct SmsfLiPoi::Impl {
    explicit Impl(li_poi::Config cfg) : runtime(std::move(cfg), make_hooks()) {}

    static li_poi::Hooks make_hooks() {
        li_poi::Hooks h;
        h.log_name = "smsf-li-poi";
        h.supported_kinds = kSupportedKinds;
        h.normalise = [](const std::string& s) { return bare_identity(s); };
        return h;
    }

    std::vector<li_poi::Match> matches(const LiIdentities& ids) const {
        std::vector<li_poi::Match> out;
        const auto add = [&](std::vector<li_poi::Match> found) {
            for (auto& m : found) {
                if (std::none_of(
                        out.begin(), out.end(), [&](const auto& o) { return o.xid == m.xid; })) {
                    out.push_back(std::move(m));
                }
            }
        };
        add(runtime.matches(ids.supi, kSupiKinds));
        if (ids.pei) {
            add(runtime.matches(pei_for_matching(*ids.pei), kPeiKinds));
        }
        if (ids.gpsi) {
            add(runtime.matches(*ids.gpsi, kGpsiKinds));
        }
        return out;
    }

    void emit(const li_poi::Match& match,
              const xiri::Event& event,
              li_core::PayloadDirection direction,
              const char* record) {
        const auto payload = xiri::encode_xiri_payload(event);
        if (!payload) {
            spdlog::error("smsf-li-poi: could not encode {} xIRI: {}", record, payload.error());
            return;
        }
        runtime.emit(match, *payload, direction, record);
    }

    li_poi::PoiRuntime runtime;
};

SmsfLiPoi::SmsfLiPoi(li_poi::Config config) : impl_(std::make_unique<Impl>(std::move(config))) {}
SmsfLiPoi::~SmsfLiPoi() = default;

void SmsfLiPoi::start() {
    impl_->runtime.start();
}
void SmsfLiPoi::stop() {
    impl_->runtime.stop();
}

std::vector<li_poi::Match>
SmsfLiPoi::report_mo_sms_message(const LiIdentities& ids,
                                 const sms_core::RpMessage& rp_data,
                                 const sms_core::MoTpdu& tpdu,
                                 const std::optional<nlohmann::json>& ue_location) {
    auto found = impl_->matches(ids);
    if (found.empty() || !rp_data.user_data) {
        return {};
    }
    xiri::SmsMessage rec;
    rec.originating_party.supi = to_xiri_supi(ids.supi);
    rec.originating_party.pei = to_xiri_pei(ids.pei);
    rec.originating_party.gpsi = to_xiri_gpsi(ids.gpsi);
    // NOTE 3: for SMS-MO the terminating party is derived from TP-DA; not a SUPI/PEI/GPSI, so the
    // sMSAddress carries the TP-DA field octets as received (clause 9.1.2.5).
    if (tpdu.destination) {
        rec.terminating_party.sms_address = tpdu.destination->raw;
    }
    rec.direction = xiri::SmsDirection::FromTarget; // NOTE 4: originated by the target UE
    // NOTE 5: transferFailed -- the SMSF cannot send MO-FORWARD-SHORT-MESSAGE to an SMS-IWMSC
    // (none exists in this project) but still generates the record.
    rec.link_transfer_status = xiri::SmsTransferStatus::TransferFailed;
    if (ue_location) {
        rec.location = location_from(*ue_location);
    }
    rec.message_type = message_type(rp_data.mti, tpdu.type);
    rec.rp_message_reference = rp_data.message_reference;

    std::vector<li_poi::Match> reported;
    for (const auto& m : found) {
        const auto task = impl_->runtime.task(m.xid);
        if (!task) {
            continue;
        }
        xiri::SmsTpduData data{false, *rp_data.user_data};
        // Clause 6.2.5.3: TruncateTPUserData -> redactedSMSTPDU for SMS-SUBMIT (an SMS-COMMAND
        // carries no TP-UD and keeps the sMSTPDU choice).
        if (task->truncate_tp_user_data && tpdu.type == sms_core::TpduType::Submit) {
            auto redacted = sms_core::redact_tpdu(*rp_data.user_data, sms_core::Direction::MsToSc);
            if (!redacted) {
                // Content is not authorised and cannot be safely redacted: the record is NOT
                // sent rather than leaking TP-UD.
                spdlog::error("smsf-li-poi: task {} requires TP-UD redaction but the TPDU could "
                              "not be redacted ({}) -- SMSMessage withheld",
                              m.xid,
                              redacted.error());
                continue;
            }
            data = xiri::SmsTpduData{true, *redacted};
        }
        rec.tpdu_data = data;
        impl_->emit(m, rec, li_core::PayloadDirection::FromTarget, "SMSMessage");
        reported.push_back(m);
    }
    return reported;
}

std::vector<li_poi::Match> SmsfLiPoi::report_mt_sms_message(const LiIdentities& ids,
                                                            const sms_core::RpMessage& rp_data,
                                                            const sms_core::MtTpdu& tpdu,
                                                            bool transfer_succeeded) {
    auto found = impl_->matches(ids);
    if (found.empty() || !rp_data.user_data) {
        return {};
    }
    xiri::SmsMessage rec;
    // NOTE 2: for SMS-MT the originating party comes from TP-OA (SMS-DELIVER) or TP-RA
    // (SMS-STATUS-REPORT) -- not a SUPI/PEI/GPSI, so sMSAddress carries the field's octets.
    if (tpdu.originating) {
        rec.originating_party.sms_address = tpdu.originating->raw;
    } else if (tpdu.recipient) {
        rec.originating_party.sms_address = tpdu.recipient->raw;
    }
    // NOTE 3: the terminating party is the UE the SMSF sends the CP-DATA_RP-DATA to.
    rec.terminating_party.supi = to_xiri_supi(ids.supi);
    rec.terminating_party.pei = to_xiri_pei(ids.pei);
    rec.terminating_party.gpsi = to_xiri_gpsi(ids.gpsi);
    rec.direction = xiri::SmsDirection::ToTarget; // NOTE 4: terminated to the target UE
    // NOTE 5 (SMS-MT): transferSucceeded when the SMSF sends the response carrying the
    // SMS-DELIVER-REPORT to the SMS-GMSC, transferFailed otherwise.
    rec.link_transfer_status = transfer_succeeded ? xiri::SmsTransferStatus::TransferSucceeded
                                                  : xiri::SmsTransferStatus::TransferFailed;
    // NOTE 6: otherMessage from TP-MMS of the TPDU sent to the UE.
    rec.other_message = tpdu.more_messages_to_send;
    rec.message_type = message_type(rp_data.mti, tpdu.type);
    rec.rp_message_reference = rp_data.message_reference;

    std::vector<li_poi::Match> reported;
    for (const auto& m : found) {
        const auto task = impl_->runtime.task(m.xid);
        if (!task) {
            continue;
        }
        xiri::SmsTpduData data{false, *rp_data.user_data};
        // Clause 6.2.5.3: TruncateTPUserData -> redactedSMSTPDU for SMS-DELIVER (an
        // SMS-STATUS-REPORT keeps the sMSTPDU choice -- the clause names SUBMIT and DELIVER only).
        if (task->truncate_tp_user_data && tpdu.type == sms_core::TpduType::Deliver) {
            auto redacted = sms_core::redact_tpdu(*rp_data.user_data, sms_core::Direction::ScToMs);
            if (!redacted) {
                spdlog::error("smsf-li-poi: task {} requires TP-UD redaction but the TPDU could "
                              "not be redacted ({}) -- SMSMessage withheld",
                              m.xid,
                              redacted.error());
                continue;
            }
            data = xiri::SmsTpduData{true, *redacted};
        }
        rec.tpdu_data = data;
        impl_->emit(m, rec, li_core::PayloadDirection::ToTarget, "SMSMessage");
        reported.push_back(m);
    }
    return reported;
}

void SmsfLiPoi::report_sms_report(const std::vector<li_poi::Match>& matches,
                                  const sms_core::RpMessage& rp_reply,
                                  const std::optional<nlohmann::json>& ue_location) {
    if (matches.empty()) {
        return;
    }
    if (!rp_reply.user_data || rp_reply.user_data->empty()) {
        spdlog::warn("smsf-li-poi: RP reply carries no TPDU -- SMSReport (sMSTPDUData Mandatory) "
                     "skipped, not fabricated");
        return;
    }
    const auto tp = sms_core::classify_tpdu((*rp_reply.user_data)[0], tpdu_direction(rp_reply.mti));
    const auto type = message_type(rp_reply.mti, tp);
    if (!type) {
        spdlog::warn("smsf-li-poi: RP reply is not an RP-ACK/RP-ERROR [SMS-SUBMIT-REPORT / "
                     "SMS-DELIVER-REPORT] -- SMSReport skipped");
        return;
    }
    xiri::SmsReport rec;
    if (ue_location) {
        rec.location = location_from(*ue_location);
    }
    // Table 6.2.5.3-3: "The sMSTPDUData.sMSTPDU field shall always be used for SMSReport records".
    rec.tpdu_data = xiri::SmsTpduData{false, *rp_reply.user_data};
    rec.message_type = *type;
    rec.rp_message_reference = rp_reply.message_reference;
    // The MO reply goes to the target UE; the MT delivery report comes from it.
    const auto direction = tpdu_direction(rp_reply.mti) == sms_core::Direction::MsToSc
                               ? li_core::PayloadDirection::FromTarget
                               : li_core::PayloadDirection::ToTarget;
    for (const auto& m : matches) {
        impl_->emit(m, rec, direction, "SMSReport");
    }
}

} // namespace smsf
