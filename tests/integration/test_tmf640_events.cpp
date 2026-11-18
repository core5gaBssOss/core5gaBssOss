// ADR-0586: TMF640 event delivery (N1-N5) to a real HTTPS listener in this process. The hub posts
// to {callback}/listener/<eventName>, and never sends an Authorization header, even when a
// process-wide token source is installed (a listener is not an SBI peer).
#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "tmf640_events.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

sbi_core::http2::TlsConfig test_tls() {
    return sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
}

struct Received {
    std::string path;
    bool had_authorization = false;
    json body;
};

} // namespace

TEST(Tmf640EventsIntegration, DeliversToTheListenerPathWithoutAToken) {
    boost::asio::io_context ioc;
    sbi_core::http2::Server listener(ioc, "127.0.0.1", 0, test_tls());
    std::mutex mutex;
    std::vector<Received> received;
    for (const char* name : {"serviceCreateEvent", "serviceDeleteEvent"}) {
        const std::string path = std::string("/cb/listener/") + name;
        listener.add_route("POST", path, [&, path](const sbi_core::http2::Request& req) {
            std::lock_guard lock(mutex);
            received.push_back(
                Received{path, req.headers.count("authorization") > 0, json::parse(req.body)});
            return sbi_core::http2::Response::json(201, "{}");
        });
    }
    listener.start();
    const auto port = listener.local_port();
    auto guard = boost::asio::make_work_guard(ioc);
    std::thread runner([&ioc] { ioc.run(); });

    sbi_core::http2::Client::set_default_token_source(
        [](const std::string&) { return std::optional<std::string>("must-not-leak"); });
    {
        provisioning::tmf640::EventHub hub(test_tls(), 4, 16);
        auto sub = hub.register_listener(
            json{{"callback", "https://127.0.0.1:" + std::to_string(port) + "/cb/"}}.dump());
        ASSERT_TRUE(sub.has_value()) << sub.error().detail;
        EXPECT_FALSE(sub->at("id").get<std::string>().empty());

        const json service{{"id", "svc-1"}, {"state", "active"}};
        hub.publish(
            provisioning::tmf640::make_event(provisioning::tmf640::kServiceCreateEvent, service));
        hub.publish(
            provisioning::tmf640::make_event(provisioning::tmf640::kServiceDeleteEvent, service));
        for (int i = 0; i < 100 && hub.delivered() + hub.failed() < 2; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        EXPECT_EQ(hub.delivered(), 2u);
        EXPECT_EQ(hub.failed(), 0u);

        // N2: after unregistering, nothing more is queued for it.
        EXPECT_TRUE(hub.unregister_listener(sub->at("id").get<std::string>()));
        EXPECT_FALSE(hub.unregister_listener(sub->at("id").get<std::string>()));
    }
    sbi_core::http2::Client::set_default_token_source({});
    guard.reset();
    ioc.stop();
    runner.join();

    std::lock_guard lock(mutex);
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].path, "/cb/listener/serviceCreateEvent");
    EXPECT_EQ(received[0].body.at("eventType"), "ServiceCreateEvent");
    EXPECT_EQ(received[0].body.at("event").at("service").at("id"), "svc-1");
    EXPECT_EQ(received[1].path, "/cb/listener/serviceDeleteEvent");
    for (const auto& r : received) {
        EXPECT_FALSE(r.had_authorization) << r.path;
    }
}
