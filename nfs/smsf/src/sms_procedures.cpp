#include "sms_procedures.hpp"

namespace smsf {

using sms_core::Bytes;
using sms_core::CpMessageType;
using sms_core::RpMessage;
using sms_core::RpMti;

tl::expected<UplinkOutcome, std::string>
process_uplink_sms(std::span<const std::uint8_t> payload,
                   const MoSmsPolicy& policy,
                   std::chrono::system_clock::time_point now) {
    auto cp = sms_core::decode_cp(payload);
    if (!cp) {
        return tl::unexpected(cp.error());
    }
    UplinkOutcome out;
    out.cp = *cp;
    if (cp->type != CpMessageType::Data) {
        return out; // CP-ACK / CP-ERROR: nothing to answer
    }
    out.downlink.push_back(sms_core::encode_cp(sms_core::make_cp_ack(*cp)));

    auto rp = sms_core::decode_rp(cp->rpdu);
    if (!rp) {
        // The CP-DATA itself was received, so it is acknowledged; an undecodable RPDU carries no
        // usable RP-Message Reference to answer with, so no RP reply is fabricated.
        out.rp_error = "RPDU: " + rp.error();
        return out;
    }
    out.rp = *rp;

    RpMessage reply;
    reply.mti = RpMti::ErrorNetworkToMs;
    reply.message_reference = rp->message_reference;
    switch (rp->mti) {
        case RpMti::DataMsToNetwork: {
            auto tpdu = sms_core::decode_mo_tpdu(*rp->user_data);
            if (!tpdu || (tpdu->type != sms_core::TpduType::Submit &&
                          tpdu->type != sms_core::TpduType::Command)) {
                reply.cause = 96; // Invalid mandatory information (table 8.4 part 1)
                break;
            }
            out.tpdu = *tpdu;
            reply.cause = policy.no_iwmsc_rp_cause;
            reply.user_data = sms_core::encode_submit_report_error(policy.no_iwmsc_tp_failure_cause,
                                                                   sms_core::encode_scts(now));
            break;
        }
        case RpMti::SmmaMsToNetwork:
            reply.cause = policy.no_iwmsc_rp_cause;
            break;
        case RpMti::AckMsToNetwork:
        case RpMti::ErrorMsToNetwork:
            return out; // MT acknowledgements: CP-ACK only
        default:
            reply.cause = 97; // Message type non-existent or not implemented (network -> MS MTI)
            break;
    }
    out.downlink.push_back(
        sms_core::encode_cp(sms_core::make_cp_data_reply(*cp, sms_core::encode_rp(reply))));
    out.rp_reply = std::move(reply);
    return out;
}

} // namespace smsf
