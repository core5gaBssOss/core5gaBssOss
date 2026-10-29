// REVIEW-5ae0ef1 SEC-08, SEC-09, SBI-04: sbi-core robustness against the shapes of request and
// peer that TS 29.500 5.2.7.2 names.
// - SEC-08: the HTTP client gives up on a peer that accepts the connection and never answers.
// - SEC-09: the server answers 413 to a request body larger than it is configured to take
//   (TS_29.500_j70.txt:2042).
// - SBI-04: a path served with other methods is 405 with Allow (:2031); a method no route serves
//   is 501 (:2028); only an unknown path is 404.
// - SBI-05: a handler's bare-path Location reaches the consumer as "{apiRoot}/<path>", apiRoot
//   being the scheme and the authority the request used (TS_29.501_j50.txt:451-456); an absolute
//   Location is passed through.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace {

sbi_core::http2::TlsConfig test_tls() {
    return sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
}

// A server on an ephemeral port, run on its own thread for the test's lifetime.
struct RunningServer {
    boost::asio::io_context ioc;
    sbi_core::http2::Server server{ioc, "127.0.0.1", 0, test_tls()};
    std::thread runner;
    std::string base() const { return "https://127.0.0.1:" + std::to_string(server.local_port()); }
    void start() {
        server.start();
        runner = std::thread([this] {
            auto guard = boost::asio::make_work_guard(ioc);
            ioc.run();
        });
    }
    ~RunningServer() {
        ioc.stop();
        if (runner.joinable()) {
            runner.join();
        }
    }
};

sbi_core::http2::ClientResponse send(sbi_core::http2::Client& c,
                                     const std::string& method,
                                     const std::string& url,
                                     const std::string& body = "") {
    sbi_core::http2::ClientRequest req;
    req.method = method;
    req.url = url;
    if (!body.empty()) {
        req.headers.emplace("content-type", "application/json");
        req.body = body;
    }
    auto r = c.send(req);
    return r.has_value() ? *r : sbi_core::http2::ClientResponse{};
}

} // namespace

TEST(SbiRobustness, TheClientGivesUpOnAPeerThatNeverAnswers) {
    // Accepts the TCP connection and then says nothing -- no TLS handshake, no response.
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::acceptor acceptor(ioc, {boost::asio::ip::make_address("127.0.0.1"), 0});
    boost::asio::ip::tcp::socket held(ioc);
    std::thread holder([&] { acceptor.accept(held); });

    sbi_core::http2::Client client(test_tls());
    client.set_timeouts(std::chrono::milliseconds(300), std::chrono::milliseconds(1000));
    const auto start = std::chrono::steady_clock::now();
    sbi_core::http2::ClientRequest req;
    req.method = "GET";
    req.url = "https://127.0.0.1:" + std::to_string(acceptor.local_endpoint().port()) + "/x";
    const auto r = client.send(req);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(r.has_value()) << "a peer that never answers is an error, not a response";
    EXPECT_LT(elapsed, std::chrono::seconds(5)) << "the client must not wait indefinitely";
    holder.join();
}

TEST(SbiRobustness, ARequestBodyAboveTheLimitIs413) {
    RunningServer s;
    s.server.set_max_body_bytes(1024);
    s.server.add_route("POST", "/x", [](const sbi_core::http2::Request&) {
        return sbi_core::http2::Response::json(200, "{}");
    });
    s.start();

    sbi_core::http2::Client client(test_tls());
    EXPECT_EQ(send(client, "POST", s.base() + "/x", std::string(100, ' ') + "{}").status, 200);
    const auto big = send(client, "POST", s.base() + "/x", std::string(4096, ' ') + "{}");
    EXPECT_EQ(big.status, 413);
    EXPECT_NE(big.body.find("MAX_JSON_SIZE_EXCEEDED"), std::string::npos) << big.body;
    EXPECT_EQ(send(client, "POST", s.base() + "/x", "{}").status, 200)
        << "the connection stays usable after a rejected body";
}

TEST(SbiRobustness, WrongMethodIs405WithAllowUnknownMethodIs501UnknownPathIs404) {
    RunningServer s;
    const auto ok = [](const sbi_core::http2::Request&) {
        return sbi_core::http2::Response::json(200, "{}");
    };
    s.server.add_route("GET", "/things/{id}", ok);
    s.server.add_route("DELETE", "/things/{id}", ok);
    s.server.add_route("POST", "/other", ok);
    s.start();

    sbi_core::http2::Client client(test_tls());
    const auto wrong = send(client, "POST", s.base() + "/things/1", "{}");
    EXPECT_EQ(wrong.status, 405) << wrong.body;
    const auto allow = wrong.headers.find("allow");
    ASSERT_NE(allow, wrong.headers.end());
    EXPECT_NE(allow->second.find("GET"), std::string::npos) << allow->second;
    EXPECT_NE(allow->second.find("DELETE"), std::string::npos) << allow->second;

    EXPECT_EQ(send(client, "PATCH", s.base() + "/things/1", "{}").status, 501)
        << "no route of this server serves PATCH";
    EXPECT_EQ(send(client, "GET", s.base() + "/nothing-here").status, 404);
}

TEST(SbiRobustness, ABarePathLocationIsMadeAbsoluteWithTheApiRoot) {
    RunningServer s;
    s.server.add_route("POST", "/things", [](const sbi_core::http2::Request&) {
        auto r = sbi_core::http2::Response::json(201, "{}");
        r.headers.emplace("location", "/things/1");
        return r;
    });
    s.server.add_route("POST", "/elsewhere", [](const sbi_core::http2::Request&) {
        auto r = sbi_core::http2::Response::json(201, "{}");
        r.headers.emplace("location", "https://other.example:443/things/2");
        return r;
    });
    s.start();

    sbi_core::http2::Client client(test_tls());
    const auto made = send(client, "POST", s.base() + "/things", "{}");
    ASSERT_EQ(made.status, 201) << made.body;
    const auto loc = made.headers.find("location");
    ASSERT_NE(loc, made.headers.end());
    EXPECT_EQ(loc->second, s.base() + "/things/1");

    const auto kept = send(client, "POST", s.base() + "/elsewhere", "{}");
    const auto abs = kept.headers.find("location");
    ASSERT_NE(abs, kept.headers.end());
    EXPECT_EQ(abs->second, "https://other.example:443/things/2");
}
