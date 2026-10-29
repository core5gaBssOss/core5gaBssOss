#include "sbi_core/nrf_heartbeat.hpp"

#include "sbi_core/sbi_headers.hpp"

#include <spdlog/spdlog.h>

#include <thread>

namespace sbi_core::nrf {

namespace {

std::string instance_url(const Registration& reg) {
    return reg.nrf_base + "/nnrf-nfm/v1/nf-instances/" + reg.nf_instance_id;
}

std::string outcome(const tl::expected<http2::ClientResponse, std::string>& resp) {
    return resp ? "HTTP " + std::to_string(resp->status) : resp.error();
}

} // namespace

bool register_instance(http2::Client& client, const Registration& reg) {
    http2::ClientRequest put;
    put.method = "PUT";
    put.url = instance_url(reg);
    put.headers.emplace("content-type", "application/json");
    put.headers.emplace(headers::kSenderTimestamp,
                        headers::format_sender_timestamp(std::chrono::system_clock::now()));
    put.body = reg.profile.dump();
    const auto resp = client.send(put);
    if (!resp || (resp->status != 200 && resp->status != 201)) {
        spdlog::warn("{}: NRF registration failed ({})", reg.log_name, outcome(resp));
        return false;
    }
    spdlog::info("{}: registered with NRF (HTTP {})", reg.log_name, resp->status);
    if (reg.on_registered) {
        reg.on_registered();
    }
    return true;
}

HeartbeatResult heartbeat(http2::Client& client, const Registration& reg) {
    http2::ClientRequest patch;
    patch.method = "PATCH";
    patch.url = instance_url(reg);
    patch.headers.emplace("content-type", "application/json-patch+json");
    patch.body =
        nlohmann::json::array(
            {nlohmann::json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
            .dump();
    const auto resp = client.send(patch);
    // 2a: 204, or 200 with the full profile.
    if (resp && (resp->status == 200 || resp->status == 204)) {
        return HeartbeatResult::Accepted;
    }
    if (resp && (resp->status == 404 || resp->status == 401)) {
        spdlog::warn(
            "{}: NRF heartbeat answered HTTP {}; registering again", reg.log_name, resp->status);
        return register_instance(client, reg) ? HeartbeatResult::Reregistered
                                              : HeartbeatResult::Failed;
    }
    spdlog::warn("{}: NRF heartbeat failed ({})", reg.log_name, outcome(resp));
    return HeartbeatResult::Failed;
}

void run_heartbeats(http2::Client& client, const Registration& reg, std::chrono::seconds interval) {
    while (true) {
        std::this_thread::sleep_for(interval);
        static_cast<void>(heartbeat(client, reg));
    }
}

} // namespace sbi_core::nrf
