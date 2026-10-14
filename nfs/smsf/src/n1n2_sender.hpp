#pragma once

#include "sbi_core/http2_client.hpp"
#include "sbi_core/oauth2_client.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Downlink SMS delivery from the SMSF to the UE's AMF: Namf_Communication_N1N2MessageTransfer
// (TS 29.518, TS29518_Namf_Communication.yaml) with n1MessageClass SMS -- TS 23.502 clause
// 4.13.3.3 steps 2c (CP-ACK) and 6a (the submit report), ADR-0486.
//
// One worker thread, one FIFO: payloads for a UE leave in the order they were queued, so the
// CP-ACK always precedes the CP-DATA carrying the RP reply (TS 24.011's CM-layer order). Sending
// runs off the SBI request path, so Nsmsf_SMService_UplinkSMS is answered without waiting on the
// AMF -- the AMF that is calling us is the same AMF these requests go to.
//
// Disclosed: the AMF is this config's `amf_base_url` (the same single-AMF lab convention
// nfs/smf/src/main.cpp uses), not an NRF lookup of UeSmsContextData.amfId; nothing is persisted, so
// a queued payload is lost on SMSF restart.

namespace smsf {

class N1n2Sender {
public:
    struct Job {
        std::string supi;                                // ueContextId in the AMF's resource path
        std::vector<std::vector<std::uint8_t>> payloads; // in send order
        bool last_is_final = false; // sets lastMsgIndication on the last payload (step 6a)
        // Called on the worker thread after each payload, with its index and whether the AMF
        // accepted it (HTTP 200/202).
        std::function<void(std::size_t index, bool accepted)> on_sent;
    };

    N1n2Sender(std::string amf_base_url,
               std::string nrf_base_url,
               std::string smsf_instance_id,
               sbi_core::http2::TlsConfig tls);
    ~N1n2Sender();
    N1n2Sender(const N1n2Sender&) = delete;
    N1n2Sender& operator=(const N1n2Sender&) = delete;

    void enqueue(Job job);

private:
    void run();
    bool send_one(const std::string& supi, const std::vector<std::uint8_t>& payload, bool last);

    std::string amf_base_url_;
    sbi_core::http2::Client client_;
    sbi_core::OAuth2Client oauth_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Job> queue_;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace smsf
