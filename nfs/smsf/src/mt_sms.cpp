#include "mt_sms.hpp"

namespace smsf {

using sms_core::CpMessageType;
using sms_core::RpMti;
using sms_core::TpduType;

tl::expected<MtRequest, std::string> parse_mt_forward_payload(std::span<const std::uint8_t> in) {
    auto cp = sms_core::decode_cp(in);
    if (!cp) {
        return tl::unexpected("not an SM-CP message: " + cp.error());
    }
    if (cp->type != CpMessageType::Data) {
        return tl::unexpected(std::string("SM-CP message is not CP-DATA"));
    }
    auto rp = sms_core::decode_rp(cp->rpdu);
    if (!rp) {
        return tl::unexpected("RPDU: " + rp.error());
    }
    if (rp->mti != RpMti::DataNetworkToMs || !rp->user_data) {
        return tl::unexpected(std::string("RPDU is not RP-DATA (network to MS)"));
    }
    auto tpdu = sms_core::decode_mt_tpdu(*rp->user_data);
    if (!tpdu) {
        return tl::unexpected("TPDU: " + tpdu.error());
    }
    if (tpdu->type != TpduType::Deliver && tpdu->type != TpduType::StatusReport) {
        return tl::unexpected(std::string("TPDU is not SMS-DELIVER or SMS-STATUS-REPORT"));
    }
    return MtRequest{*cp, *rp, *tpdu};
}

sms_core::Bytes make_mt_cp_data(const MtRequest& request, std::uint8_t ti_value) {
    sms_core::CpMessage out;
    out.ti_flag = false;
    out.ti_value = ti_value;
    out.type = CpMessageType::Data;
    out.rpdu = request.cp.rpdu;
    return sms_core::encode_cp(out);
}

sms_core::Bytes make_delivery_report_payload(const MtRequest& request,
                                             const sms_core::Bytes& ue_rpdu) {
    return sms_core::encode_cp(sms_core::make_cp_data_reply(request.cp, ue_rpdu));
}

tl::expected<MtReport, MtFailure>
MtTransactions::run(const std::string& supi,
                    const std::function<void(std::uint8_t)>& send_downlink) {
    const auto deadline = std::chrono::steady_clock::now() + policy_.tr1n;
    std::uint8_t ti = 0;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (in_flight_ >= policy_.max_concurrent) {
            return tl::unexpected(MtFailure::Congestion);
        }
        ++in_flight_;
        if (!cv_.wait_until(lock, deadline, [&] { return !slots_[supi].busy; })) {
            --in_flight_;
            return tl::unexpected(MtFailure::Timeout);
        }
        Slot& slot = slots_[supi];
        slot.busy = true;
        slot.ti_value = slot.next_ti;
        slot.next_ti = static_cast<std::uint8_t>((slot.next_ti + 1) % 7); // TIO 0..6
        slot.cp_acked = false;
        slot.failure.reset();
        slot.report.reset();
        ti = slot.ti_value;
    }

    send_downlink(ti);

    std::unique_lock<std::mutex> lock(mutex_);
    Slot& slot = slots_[supi];
    cv_.wait_until(lock, deadline, [&] { return slot.failure || slot.report; });
    tl::expected<MtReport, MtFailure> result = tl::unexpected(MtFailure::Timeout);
    if (slot.report) {
        result = std::move(*slot.report);
    } else if (slot.failure) {
        result = tl::unexpected(*slot.failure);
    }
    slot.busy = false;
    slot.failure.reset();
    slot.report.reset();
    --in_flight_;
    lock.unlock();
    cv_.notify_all();
    return result;
}

void MtTransactions::downlink_result(const std::string& supi,
                                     std::uint8_t ti_value,
                                     bool accepted) {
    if (accepted) {
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto it = slots_.find(supi);
        if (it == slots_.end() || !it->second.busy || it->second.ti_value != ti_value ||
            it->second.report) {
            return;
        }
        it->second.failure = MtFailure::NotDelivered;
    }
    cv_.notify_all();
}

bool MtTransactions::on_uplink(const std::string& supi,
                               const sms_core::CpMessage& cp,
                               const std::optional<sms_core::RpMessage>& rp,
                               const std::string& ue_location_json) {
    if (!cp.ti_flag) {
        return false; // UE-originated TI: an MO transaction, not ours
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto it = slots_.find(supi);
        if (it == slots_.end() || !it->second.busy || it->second.ti_value != cp.ti_value) {
            return false;
        }
        Slot& slot = it->second;
        if (slot.report || slot.failure) {
            return true; // already settled; a late duplicate
        }
        switch (cp.type) {
            case CpMessageType::Ack:
                slot.cp_acked = true;
                return true;
            case CpMessageType::Error:
                slot.failure = MtFailure::CpError;
                break;
            case CpMessageType::Data:
                if (!rp ||
                    (rp->mti != RpMti::AckMsToNetwork && rp->mti != RpMti::ErrorMsToNetwork)) {
                    return true; // not a delivery report: acknowledged at CP level only
                }
                // TS 24.011 clause 5.3.4: a missing first CP-ACK "may be interpreted" from the
                // reception of the CP-DATA.
                slot.cp_acked = true;
                slot.report = MtReport{*rp, cp.rpdu, ue_location_json};
                break;
        }
    }
    cv_.notify_all();
    return true;
}

std::size_t MtTransactions::in_flight() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return in_flight_;
}

} // namespace smsf
