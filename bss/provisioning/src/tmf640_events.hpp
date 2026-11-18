#pragma once
// TMF640 v4.0.0 event notifications (ADR-0586; procedure list N1-N5 approved by the user on
// 2026-11-18). Source: specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json.
//
//   N1  POST /hub        EventSubscriptionInput{callback, query} -> 201 EventSubscription
//   N2  DELETE /hub/{id} 204, or 404 for an unknown id
//   N3  POST /service succeeded             -> ServiceCreateEvent
//   N4  PATCH /service/{id} succeeded       -> ServiceAttributeValueChangeEvent, plus
//                                              ServiceStateChangeEvent when `state` changed
//   N5  DELETE /service/{id} succeeded      -> ServiceDeleteEvent
//
// Delivery: POST {callback}/listener/<eventName in lowerCamelCase>, the client-side paths the
// swagger lists under /listener/*. That URL shape is a reading of the TMF convention, not swagger
// text. Delivery is best effort, from one worker thread with a bounded queue. There is no retry,
// and a failed or dropped delivery is logged and counted. `query` is stored and echoed but not
// applied as a filter. Listeners are kept in memory, so a restart forgets them.
//
// The callback client never sends an Authorization header: a listener is not an SBI peer, so no
// NRF token may leave the process to a URL the API caller chose.
#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <tl/expected.hpp>

#include "tmf640_service.hpp"

namespace provisioning::tmf640 {

inline constexpr const char* kServiceCreateEvent = "ServiceCreateEvent";
inline constexpr const char* kServiceAttributeValueChangeEvent = "ServiceAttributeValueChangeEvent";
inline constexpr const char* kServiceStateChangeEvent = "ServiceStateChangeEvent";
inline constexpr const char* kServiceDeleteEvent = "ServiceDeleteEvent";

// "ServiceCreateEvent" -> "/listener/serviceCreateEvent". Pure.
std::string listener_path(const std::string& event_type);

// The notification body for `event_type` about `service`: eventId, eventTime, eventType and
// event.service. The optional fieldPath, correlationId, domain, title, description and priority
// are not set. Pure apart from the id and the clock.
nlohmann::json make_event(const std::string& event_type, const nlohmann::json& service);

// Validates a POST /hub body (EventSubscriptionInput): a JSON object whose `callback` is an
// absolute https URL, with an optional string `query`. Pure.
tl::expected<nlohmann::json, ServiceError> validate_listener(const std::string& body);

class EventHub {
public:
    EventHub(sbi_core::http2::TlsConfig tls, std::size_t max_listeners, std::size_t max_queue);
    ~EventHub();
    EventHub(const EventHub&) = delete;
    EventHub& operator=(const EventHub&) = delete;

    // N1. 201 body on success; 400 for a bad body; 409 when max_listeners are registered.
    tl::expected<nlohmann::json, ServiceError> register_listener(const std::string& body);
    // N2. false when no listener has this id.
    bool unregister_listener(const std::string& id);

    // N3-N5: queues one delivery per registered listener. Never blocks on the network.
    void publish(const nlohmann::json& event);

    std::size_t delivered() const { return delivered_.load(); }
    std::size_t failed() const { return failed_.load(); }
    std::size_t dropped() const { return dropped_.load(); }

private:
    struct Delivery {
        std::string url;
        std::string body;
    };
    void run();

    sbi_core::http2::Client client_;
    const std::size_t max_listeners_;
    const std::size_t max_queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::string, nlohmann::json> listeners_; // id -> EventSubscription
    std::deque<Delivery> queue_;
    bool stopping_ = false;
    std::atomic<std::size_t> delivered_{0};
    std::atomic<std::size_t> failed_{0};
    std::atomic<std::size_t> dropped_{0};
    std::thread worker_;
};

} // namespace provisioning::tmf640
