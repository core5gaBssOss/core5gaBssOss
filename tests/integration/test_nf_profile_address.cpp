// ADR-0518: an NF registers its configured advertised_ipv4 in the NFProfile (TS 29.510
// ipv4Addresses), not a hardcoded 127.0.0.1. Before this, 23 NFs registered 127.0.0.1 whatever
// their config said, so a consumer that discovered them through the NRF got an address only the
// NF's own host could reach. The real EIR binary (no datastore) registers with an in-process NRF
// stand-in on an ephemeral port, which records the NFProfile it receives.
#include "sbi_core/http2_server.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "spawn_guard.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

class FakeNrf {
public:
    FakeNrf()
        : server_(ioc_,
                  "127.0.0.1",
                  0,
                  sbi_core::http2::TlsConfig{
                      .cert_path = CERTS_DIR "/hello-nf/cert.pem",
                      .key_path = CERTS_DIR "/hello-nf/key.pem",
                      .ca_path = CERTS_DIR "/ca/ca.crt",
                  }) {
        const std::string path = "/nnrf-nfm/v1/nf-instances/{nfInstanceID}";
        server_.add_route("PUT", path, [this](const sbi_core::http2::Request& req) {
            const std::lock_guard lock(mutex_);
            profiles_[req.path_params.at("nfInstanceID")] = json::parse(req.body);
            return sbi_core::http2::Response::json(201, req.body);
        });
        server_.add_route("PATCH", path, [](const sbi_core::http2::Request&) {
            return sbi_core::http2::Response::json(200, "{}");
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

    // The first NFProfile registered, waiting up to `timeout` for it.
    std::optional<json> first_profile(std::chrono::seconds timeout) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            {
                const std::lock_guard lock(mutex_);
                if (!profiles_.empty()) {
                    return profiles_.begin()->second;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return std::nullopt;
    }

private:
    std::mutex mutex_;
    std::map<std::string, json> profiles_;
    boost::asio::io_context ioc_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard_{
        boost::asio::make_work_guard(ioc_)};
    sbi_core::http2::Server server_;
    std::thread thread_;
};

// config/eir.json on this test's own ports, registering with `nrf`.
std::string eir_config(const FakeNrf& nrf) {
    std::ifstream in(EIR_CONFIG_TEMPLATE);
    json cfg = json::parse(in);
    cfg["port"] = 28761;
    cfg["metrics_bind_address"] = "127.0.0.1:28762";
    cfg["nrf_base_url"] = nrf.base();
    const auto path =
        (std::filesystem::temp_directory_path() / "nf_profile_address_eir.json").string();
    std::ofstream(path) << cfg.dump();
    return path;
}

// Spawns the EIR with EIR_ADVERTISED_IPV4=`setting`.
nf_test::SpawnedProcess spawn_eir(const FakeNrf& nrf, const std::string& setting) {
    const auto cfg = eir_config(nrf);
    ::setenv("EIR_CONFIG_FILE", cfg.c_str(), 1);
    ::setenv("EIR_ADVERTISED_IPV4", setting.c_str(), 1);
    nf_test::SpawnedProcess eir(EIR_PATH);
    ::unsetenv("EIR_ADVERTISED_IPV4");
    ::unsetenv("EIR_CONFIG_FILE");
    return eir;
}

TEST(NfProfileAddress, ALiteralAdvertisedAddressIsWhatTheNfRegisters) {
    FakeNrf nrf;
    auto eir = spawn_eir(nrf, "192.0.2.10"); // TEST-NET-1: never this host's address
    const auto profile = nrf.first_profile(std::chrono::seconds(20));
    ASSERT_TRUE(profile.has_value()) << "the EIR never registered";
    EXPECT_EQ(profile->at("ipv4Addresses"), json::array({"192.0.2.10"}));
}

TEST(NfProfileAddress, AutoRegistersTheLocalAddressTowardTheNrf) {
    FakeNrf nrf; // on 127.0.0.1, so the route to it leaves from 127.0.0.1
    auto eir = spawn_eir(nrf, "auto");
    const auto profile = nrf.first_profile(std::chrono::seconds(20));
    ASSERT_TRUE(profile.has_value()) << "the EIR never registered";
    EXPECT_EQ(profile->at("ipv4Addresses"), json::array({"127.0.0.1"}));
}

TEST(NfProfileAddress, AHostnameStopsTheNfInsteadOfRegisteringAGuess) {
    FakeNrf nrf;
    auto eir = spawn_eir(nrf, "eir");
    // Exited within 10 s? WNOWAIT leaves the child to wait_for_exit(), which reaps it.
    siginfo_t info{};
    bool exited = false;
    for (int i = 0; i < 100 && !exited; ++i) {
        info.si_pid = 0;
        exited = ::waitid(P_PID, eir.pid(), &info, WEXITED | WNOHANG | WNOWAIT) == 0 &&
                 info.si_pid == eir.pid();
        if (!exited) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    ASSERT_TRUE(exited) << "the EIR kept running with a hostname as its advertised address";
    const int status = eir.wait_for_exit();
    EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) != 0) << "status " << status;
    EXPECT_FALSE(nrf.first_profile(std::chrono::seconds(1)).has_value());
}

} // namespace
