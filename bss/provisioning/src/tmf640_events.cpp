#include "tmf640_events.hpp"

#include "sbi_core/datetime.hpp"
#include "sbi_core/uuid.hpp"

#include <spdlog/spdlog.h>

#include <cctype>
#include <chrono>
#include <optional>
#include <utility>

namespace provisioning::tmf640 {

using nlohmann::json;

std::string listener_path(const std::string& event_type) {
    std::string name = event_type;
    if (!name.empty()) {
        name[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
    }
    return "/listener/" + name;
}

json make_event(const std::string& event_type, const json& service) {
    json event{{"eventId", sbi_core::generate_uuid_v4()},
               {"eventTime", sbi_core::format_rfc3339(std::chrono::system_clock::now())},
               {"eventType", event_type},
               {"event", {{"service", service}}}};
    return event;
}

tl::expected<json, ServiceError> validate_listener(const std::string& body) {
    const auto bad = [](const std::string& detail) {
        return tl::unexpected(ServiceError{400, "Bad Request", detail});
    };
    json in;
    try {
        in = json::parse(body);
    } catch (const std::exception&) {
        return bad("request body is not valid JSON");
    }
    if (!in.is_object()) {
        return bad("EventSubscriptionInput must be a JSON object");
    }
    const auto cb = in.find("callback");
    if (cb == in.end() || !cb->is_string()) {
        return bad("EventSubscriptionInput.callback is required and must be a string");
    }
    const auto callback = cb->get<std::string>();
    if (callback.rfind("https://", 0) != 0 || callback.size() <= 8 || callback.size() > 2048 ||
        callback.find_first_of(" \t\r\n") != std::string::npos) {
        return bad("callback must be an absolute https URL");
    }
    json out{{"callback", callback}};
    if (const auto q = in.find("query"); q != in.end() && !q->is_null()) {
        if (!q->is_string()) {
            return bad("EventSubscriptionInput.query must be a string");
        }
        out["query"] = *q;
    }
    return out;
}

EventHub::EventHub(sbi_core::http2::TlsConfig tls, std::size_t max_listeners, std::size_t max_queue)
    : client_(std::move(tls)), max_listeners_(max_listeners), max_queue_(max_queue) {
    // No token to a listener: it is not an SBI peer (see the header).
    client_.set_token_source([](const std::string&) { return std::optional<std::string>(); });
    client_.set_timeouts(std::chrono::milliseconds(2000), std::chrono::milliseconds(5000));
    worker_ = std::thread([this] { run(); });
}

EventHub::~EventHub() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

tl::expected<json, ServiceError> EventHub::register_listener(const std::string& body) {
    auto valid = validate_listener(body);
    if (!valid.has_value()) {
        return tl::unexpected(valid.error());
    }
    std::lock_guard lock(mutex_);
    if (listeners_.size() >= max_listeners_) {
        return tl::unexpected(
            ServiceError{409, "Conflict", "the listener limit of this server is reached"});
    }
    json sub = *valid;
    sub["id"] = sbi_core::generate_uuid_v4();
    listeners_.emplace(sub.at("id").get<std::string>(), sub);
    return sub;
}

bool EventHub::unregister_listener(const std::string& id) {
    std::lock_guard lock(mutex_);
    return listeners_.erase(id) > 0;
}

void EventHub::publish(const json& event) {
    const auto path = listener_path(event.value("eventType", std::string()));
    const auto body = event.dump();
    {
        std::lock_guard lock(mutex_);
        for (const auto& [id, sub] : listeners_) {
            if (queue_.size() >= max_queue_) {
                ++dropped_;
                spdlog::warn("provisioning: TMF640 event queue full; {} to listener {} dropped",
                             event.value("eventType", std::string()),
                             id);
                continue;
            }
            auto base = sub.at("callback").get<std::string>();
            while (!base.empty() && base.back() == '/') {
                base.pop_back();
            }
            queue_.push_back(Delivery{base + path, body});
        }
    }
    cv_.notify_one();
}

void EventHub::run() {
    for (;;) {
        Delivery d;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) {
                return; // undelivered events are dropped on shutdown (best effort)
            }
            d = std::move(queue_.front());
            queue_.pop_front();
        }
        sbi_core::http2::ClientRequest req;
        req.method = "POST";
        req.url = d.url;
        req.headers.emplace("content-type", "application/json");
        req.body = std::move(d.body);
        auto resp = client_.send(req);
        if (resp.has_value() && resp->status >= 200 && resp->status < 300) {
            ++delivered_;
        } else {
            ++failed_;
            spdlog::warn("provisioning: TMF640 event delivery to {} failed: {}",
                         d.url,
                         resp.has_value() ? std::to_string(resp->status) : resp.error());
        }
    }
}

} // namespace provisioning::tmf640
