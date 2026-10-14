// ADR-0490: TS 29.500 5.2.2.2 User-Agent. nf_config::load sets SBI_USER_AGENT to the service's
// TS 29.510 NFType (only when the service is an NF); the SBI client adds it to every request that
// does not already carry a User-Agent.
#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"

#include <boost/asio.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "nf_config/nf_config.hpp"

#include <gtest/gtest.h>

namespace {

sbi_core::http2::TlsConfig test_tls() {
    return sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
}

// The User-Agent nf_config::load leaves behind for `service`, "" when none.
std::string user_agent_after_load(const std::string& service) {
    ::unsetenv("SBI_USER_AGENT");
    const std::string dir = std::string(::testing::TempDir()) + "ua_cfg";
    std::filesystem::create_directories(dir);
    std::ofstream(dir + "/" + service + ".json") << "{}";
    (void)nf_config::load(service, dir);
    const char* ua = std::getenv("SBI_USER_AGENT");
    std::string out = ua == nullptr ? "" : ua;
    ::unsetenv("SBI_USER_AGENT");
    return out;
}

// Runs a server on an ephemeral port that records the last User-Agent it saw.
struct RunningServer {
    boost::asio::io_context ioc;
    sbi_core::http2::Server server;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard;
    std::thread thread;
    std::mutex mutex;
    std::string seen;
    RunningServer()
        : server(ioc, "127.0.0.1", 0, test_tls()), guard(boost::asio::make_work_guard(ioc)) {
        server.add_route("GET", "/ping", [this](const sbi_core::http2::Request& req) {
            const auto it = req.headers.find("user-agent");
            const std::lock_guard<std::mutex> lock(mutex);
            seen = it == req.headers.end() ? "<none>" : it->second;
            return sbi_core::http2::Response::json(200, R"({"ok":true})");
        });
        server.start();
        thread = std::thread([this] { ioc.run(); });
    }
    ~RunningServer() {
        guard.reset();
        ioc.stop();
        thread.join();
    }
    std::string ping(const std::string& explicit_ua = "") {
        sbi_core::http2::Client client(test_tls());
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = "https://127.0.0.1:" + std::to_string(server.local_port()) + "/ping";
        if (!explicit_ua.empty()) {
            req.headers.emplace("User-Agent", explicit_ua);
        }
        const auto resp = client.send(req);
        EXPECT_TRUE(resp.has_value() && resp->status == 200);
        const std::lock_guard<std::mutex> lock(mutex);
        return seen;
    }
};

} // namespace

TEST(SbiUserAgent, ConfigLoadSetsTheNfTypeOnlyForAnNf) {
    EXPECT_EQ(user_agent_after_load("amf"), "AMF");
    EXPECT_EQ(user_agent_after_load("nwdaf"), "NWDAF");
    EXPECT_EQ(user_agent_after_load("eir"), "5G_EIR"); // the TS 29.510 NFType value
    EXPECT_EQ(user_agent_after_load("hello-nf"), "");  // not an NF: no invented type
    EXPECT_EQ(user_agent_after_load("li-admf"), "");
}

TEST(SbiUserAgent, ClientSendsItUnlessTheRequestCarriesOne) {
    RunningServer srv;
    ::unsetenv("SBI_USER_AGENT");
    EXPECT_EQ(srv.ping(), "<none>");
    ::setenv("SBI_USER_AGENT", "SMF", 1);
    EXPECT_EQ(srv.ping(), "SMF");
    EXPECT_EQ(srv.ping("PCF-explicit"), "PCF-explicit"); // the caller's own value wins
    ::unsetenv("SBI_USER_AGENT");
}
