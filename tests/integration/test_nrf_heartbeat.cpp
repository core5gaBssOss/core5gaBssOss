// ADR-0506: the NF side of the NRF heart-beat (TS 29.510 5.2.2.3.2) re-registers an instance the
// NRF no longer knows. An in-process NRF stand-in on an ephemeral port answers as nfs/nrf does:
// PATCH of an unknown instance 404 (step 2b), a request without a token 401 (ADR-0499 phase 4),
// NFRegister of an unknown instance open, replacing a known one only with a token.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/nrf_heartbeat.hpp"

#include <boost/asio/io_context.hpp>

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace {

using nlohmann::json;
using sbi_core::nrf::HeartbeatResult;

sbi_core::http2::TlsConfig test_tls() {
    return sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
}

const std::string kInstance = "4947a69a-f61b-4bc1-b9da-47c9c5d14b64";

class FakeNrf {
public:
    FakeNrf() : server_(ioc_, "127.0.0.1", 0, test_tls()) {
        const std::string path = "/nnrf-nfm/v1/nf-instances/{nfInstanceID}";
        server_.add_route("PATCH", path, [this](const sbi_core::http2::Request& req) {
            const std::lock_guard lock(mutex_);
            ++patches;
            if (!has_token(req)) {
                return sbi_core::http2::Response::json(401, "{}");
            }
            if (!registry.contains(req.path_params.at("nfInstanceID"))) {
                return sbi_core::http2::Response::json(404, "{}");
            }
            return sbi_core::http2::Response::json(200, "{}");
        });
        server_.add_route("PUT", path, [this](const sbi_core::http2::Request& req) {
            const std::lock_guard lock(mutex_);
            ++puts;
            const auto id = req.path_params.at("nfInstanceID");
            const bool known = registry.contains(id);
            if (known && !has_token(req)) {
                return sbi_core::http2::Response::json(401, "{}");
            }
            registry[id] = json::parse(req.body);
            return sbi_core::http2::Response::json(known ? 200 : 201, req.body);
        });
        server_.start();
        thread_ = std::thread([this] { ioc_.run(); });
    }

    ~FakeNrf() {
        ioc_.stop();
        thread_.join();
    }

    FakeNrf(const FakeNrf&) = delete;
    FakeNrf& operator=(const FakeNrf&) = delete;

    std::string base() const { return "https://127.0.0.1:" + std::to_string(server_.local_port()); }

    // The NRF restarted: its in-memory registry is empty.
    void restart() {
        const std::lock_guard lock(mutex_);
        registry.clear();
    }

    std::atomic<bool> tokens{true}; // whether the NF's requests carry a token
    int patches = 0;
    int puts = 0;
    std::map<std::string, json> registry;
    std::mutex mutex_;

private:
    bool has_token(const sbi_core::http2::Request& req) const {
        return tokens || req.headers.contains("authorization");
    }

    boost::asio::io_context ioc_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard_{
        boost::asio::make_work_guard(ioc_)};
    sbi_core::http2::Server server_;
    std::thread thread_;
};

class NrfHeartbeat : public ::testing::Test {
protected:
    NrfHeartbeat() {
        reg.nrf_base = nrf.base();
        reg.nf_instance_id = kInstance;
        reg.profile = json{{"nfInstanceId", kInstance},
                           {"nfType", "AMF"},
                           {"nfStatus", "REGISTERED"},
                           {"heartBeatTimer", 30}};
        reg.log_name = "test";
        reg.on_registered = [this] { ++registered_calls; };
    }

    int puts() {
        const std::lock_guard lock(nrf.mutex_);
        return nrf.puts;
    }

    FakeNrf nrf;
    sbi_core::http2::Client client{test_tls()};
    sbi_core::nrf::Registration reg;
    std::atomic<int> registered_calls{0};
};

TEST_F(NrfHeartbeat, ARegisteredInstanceIsOnlyHeartbeaten) {
    ASSERT_TRUE(sbi_core::nrf::register_instance(client, reg));
    EXPECT_EQ(registered_calls, 1);
    EXPECT_EQ(sbi_core::nrf::heartbeat(client, reg), HeartbeatResult::Accepted);
    EXPECT_EQ(puts(), 1);
    EXPECT_EQ(registered_calls, 1);
}

TEST_F(NrfHeartbeat, AfterAnNrfRestartThe404HeartbeatRegistersTheSameProfileAgain) {
    ASSERT_TRUE(sbi_core::nrf::register_instance(client, reg));
    nrf.restart();
    EXPECT_EQ(sbi_core::nrf::heartbeat(client, reg), HeartbeatResult::Reregistered);
    EXPECT_EQ(puts(), 2);
    EXPECT_EQ(registered_calls, 2);
    {
        const std::lock_guard lock(nrf.mutex_);
        EXPECT_EQ(nrf.registry.at(kInstance), reg.profile);
    }
    EXPECT_EQ(sbi_core::nrf::heartbeat(client, reg), HeartbeatResult::Accepted);
    EXPECT_EQ(puts(), 2);
}

TEST_F(NrfHeartbeat, ATokenlessHeartbeatTheNrfRefuses401AlsoRegistersAgain) {
    // The NRF lost the instance and, not knowing it, issues it no token (ADR-0499): the heartbeat
    // is answered 401 rather than 404. NFRegister of an unknown instance needs no token.
    ASSERT_TRUE(sbi_core::nrf::register_instance(client, reg));
    nrf.restart();
    nrf.tokens = false;
    EXPECT_EQ(sbi_core::nrf::heartbeat(client, reg), HeartbeatResult::Reregistered);
    EXPECT_EQ(registered_calls, 2);
}

TEST_F(NrfHeartbeat, A401ForAnInstanceTheNrfStillKnowsChangesNothing) {
    ASSERT_TRUE(sbi_core::nrf::register_instance(client, reg));
    nrf.tokens = false;
    // Replacing a known profile needs a token too, so the PUT is refused and the profile kept.
    EXPECT_EQ(sbi_core::nrf::heartbeat(client, reg), HeartbeatResult::Failed);
    EXPECT_EQ(puts(), 2);
    EXPECT_EQ(registered_calls, 1);
}

TEST_F(NrfHeartbeat, AnUnreachableNrfIsAFailedHeartbeatWithoutARegistration) {
    sbi_core::nrf::Registration nowhere = reg;
    nowhere.nrf_base = "https://127.0.0.1:1"; // nothing listens on port 1
    EXPECT_EQ(sbi_core::nrf::heartbeat(client, nowhere), HeartbeatResult::Failed);
    EXPECT_EQ(puts(), 0);
    EXPECT_EQ(registered_calls, 0);
}

} // namespace
