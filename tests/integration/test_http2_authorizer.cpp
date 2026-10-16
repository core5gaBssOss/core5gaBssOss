// ADR-0499 phase 1: http2::Server runs its Authorizer before an OAuth2 route's handler and never
// for a None route (notifications, TS 33.501 13.4.1.0). In-process server on an OS-chosen port.
#include "sbi_core/authorizer.hpp"
#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/jwt.hpp"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

#include <gtest/gtest.h>

namespace {

constexpr const char* kNrfId = "6fa459ea-ee8a-3ca4-894e-db77e160355e";

sbi_core::http2::TlsConfig test_tls() {
    return sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
}

void write_pem(EVP_PKEY* key, bool is_private, const std::filesystem::path& path) {
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
    if (is_private) {
        PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr);
    } else {
        PEM_write_bio_PUBKEY(bio.get(), key);
    }
    char* data = nullptr;
    const long len = BIO_get_mem_data(bio.get(), &data);
    std::ofstream(path, std::ios::binary).write(data, len);
}

TEST(Http2Authorizer, TheServerChecksProtectedRoutesBeforeTheHandlerAndSkipsNotifications) {
    const auto dir =
        std::filesystem::temp_directory_path() / ("http2_authz_" + std::to_string(::getpid()));
    std::filesystem::create_directories(dir);
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_EC_gen("P-256"), EVP_PKEY_free);
    ASSERT_NE(key, nullptr);
    write_pem(key.get(), true, dir / "priv.pem");
    write_pem(key.get(), false, dir / "pub.pem");

    boost::asio::io_context ioc;
    sbi_core::http2::Server server(ioc, "127.0.0.1", 0, test_tls());
    std::atomic<int> protected_calls{0};
    server.add_route("POST", "/npcf-am-policy-control/v1/policies", [&](const auto&) {
        ++protected_calls;
        return sbi_core::http2::Response::json(201, "{}");
    });
    server.add_route(
        "POST",
        "/callbacks/notify",
        [](const auto&) { return sbi_core::http2::Response::json(204, ""); },
        sbi_core::http2::RouteAuth::None);
    server.set_authorizer(std::make_shared<const sbi_core::http2::Authorizer>(
        sbi_core::http2::AuthorizerConfig{.required = true, .nf_type = "PCF"},
        std::make_shared<sbi_core::jwt::Verifier>((dir / "pub.pem").string(), kNrfId)));
    server.start();
    const auto base = "https://127.0.0.1:" + std::to_string(server.local_port());
    auto guard = boost::asio::make_work_guard(ioc);
    std::thread runner([&ioc] { ioc.run(); });

    sbi_core::http2::Client client(test_tls());
    const auto post = [&](const std::string& path, const std::string& token) {
        sbi_core::http2::ClientRequest req;
        req.method = "POST";
        req.url = base + path;
        req.body = "{}";
        req.headers.emplace("content-type", "application/json");
        if (!token.empty()) {
            req.headers.emplace("authorization", "Bearer " + token);
        }
        return client.send(req);
    };

    const auto no_token = post("/npcf-am-policy-control/v1/policies", "");
    ASSERT_TRUE(no_token.has_value());
    EXPECT_EQ(no_token->status, 401);
    EXPECT_EQ(no_token->headers.count("www-authenticate"), 1U);
    EXPECT_EQ(protected_calls.load(), 0) << "a rejected request must not reach the handler";

    const auto notify = post("/callbacks/notify", "");
    ASSERT_TRUE(notify.has_value());
    EXPECT_EQ(notify->status, 204);

    sbi_core::jwt::Issuer issuer((dir / "priv.pem").string(), kNrfId);
    const auto token = issuer.issue("1d2c3b4a-0000-4000-8000-000000000001",
                                    "PCF",
                                    "npcf-am-policy-control",
                                    std::chrono::seconds(60));
    const auto ok = post("/npcf-am-policy-control/v1/policies", token.token);
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok->status, 201);
    EXPECT_EQ(protected_calls.load(), 1);

    guard.reset();
    ioc.stop();
    runner.join();
    std::filesystem::remove_all(dir);
}

} // namespace
