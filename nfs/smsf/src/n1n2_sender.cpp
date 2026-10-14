#include "n1n2_sender.hpp"

#include "sbi_core/multipart.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "TS26510_CommonData_grp.hpp" // N1N2MessageTransferReqData (TS 29.518 schema)
#include "TS29518_Namf_MT.hpp"        // EnableUeReachabilityReqData / RspData

namespace smsf {

N1n2Sender::N1n2Sender(std::string amf_base_url,
                       std::string nrf_base_url,
                       std::string smsf_instance_id,
                       sbi_core::http2::TlsConfig tls)
    : amf_base_url_(std::move(amf_base_url)), client_(std::move(tls)),
      oauth_(client_, nrf_base_url + "/oauth2/token", smsf_instance_id, "namf-comm", "AMF"),
      mt_oauth_(
          client_, nrf_base_url + "/oauth2/token", std::move(smsf_instance_id), "namf-mt", "AMF"),
      worker_([this] { run(); }) {}

N1n2Sender::~N1n2Sender() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    worker_.join();
}

void N1n2Sender::enqueue(Job job) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(job));
    }
    cv_.notify_one();
}

void N1n2Sender::run() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) {
                return;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
        }
        for (std::size_t i = 0; i < job.payloads.size(); ++i) {
            const bool last = job.last_is_final && i + 1 == job.payloads.size();
            const bool ok = send_one(job.supi, job.payloads[i], last);
            if (job.on_sent) {
                job.on_sent(i, ok);
            }
            if (!ok) {
                break; // CM-layer order: never send the RP reply without its CP-ACK
            }
        }
    }
}

tl::expected<std::string, std::string> N1n2Sender::enable_ue_reachability(const std::string& supi) {
    auto token = mt_oauth_.get_bearer_token();
    if (!token) {
        return tl::unexpected("no namf-mt token: " + token.error());
    }
    sbi_gen::EnableUeReachabilityReqData req{};
    req.reachability.value = sbi_gen::UeReachability::REACHABLE;

    sbi_core::http2::ClientRequest http;
    http.method = "PUT";
    http.url = amf_base_url_ + "/namf-mt/v1/ue-contexts/" + supi + "/ue-reachind";
    http.headers.emplace("content-type", "application/json");
    http.headers.emplace("authorization", "Bearer " + *token);
    http.body = nlohmann::json(req).dump();
    auto resp = client_.send(http);
    if (!resp) {
        return tl::unexpected("AMF not reachable: " + resp.error());
    }
    if (resp->status != 200) {
        return tl::unexpected("AMF answered HTTP " + std::to_string(resp->status));
    }
    try {
        return nlohmann::json::parse(resp->body)
            .get<sbi_gen::EnableUeReachabilityRspData>()
            .reachability.value;
    } catch (const nlohmann::json::exception& e) {
        return tl::unexpected(std::string("malformed EnableUeReachabilityRspData: ") + e.what());
    }
}

bool N1n2Sender::send_one(const std::string& supi,
                          const std::vector<std::uint8_t>& payload,
                          bool last) {
    auto token = oauth_.get_bearer_token();
    if (!token) {
        spdlog::warn("smsf: no AMF token, downlink SMS for {} not sent: {}", supi, token.error());
        return false;
    }
    sbi_gen::N1N2MessageTransferReqData req{};
    sbi_gen::N1MessageContainer container{};
    container.n1MessageClass.value = sbi_gen::N1MessageClass::SMS;
    container.n1MessageContent.contentId = "n1Message";
    req.n1MessageContainer = container;
    if (last) {
        req.lastMsgIndication = true;
    }

    sbi_core::multipart::Part json_part;
    json_part.content_type = "application/json";
    json_part.body = nlohmann::json(req).dump();
    sbi_core::multipart::Part bin_part;
    // TS29518_Namf_Communication.yaml encoding of binaryDataN1Message.
    bin_part.content_type = "application/vnd.3gpp.5gnas";
    bin_part.content_id = "n1Message";
    bin_part.body.assign(payload.begin(), payload.end());
    const auto encoded = sbi_core::multipart::encode({json_part, bin_part});

    sbi_core::http2::ClientRequest http;
    http.method = "POST";
    http.url = amf_base_url_ + "/namf-comm/v1/ue-contexts/" + supi + "/n1-n2-messages";
    http.headers.emplace("content-type", encoded.content_type_header);
    http.headers.emplace("authorization", "Bearer " + *token);
    http.body = encoded.body;
    auto resp = client_.send(http);
    if (!resp || (resp->status != 200 && resp->status != 202)) {
        spdlog::warn("smsf: N1N2MessageTransfer (SMS) to AMF failed for {}: {}",
                     supi,
                     resp ? std::to_string(resp->status) : resp.error());
        return false;
    }
    return true;
}

} // namespace smsf
