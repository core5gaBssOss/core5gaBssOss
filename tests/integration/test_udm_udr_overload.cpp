// ADR-0602 (SBI-03, REVIEW-5ae0ef1): TS 29.500 6.4.2.1 -- an NF Service Producer that cannot fulfil
// a request because another producer answered 503 or 429 rejects it with 502 Bad Gateway and
// cause INBOUND_SERVER_ERROR. Here the UDM is the producer and the UDR the inbound server: real nrf
// and udm processes, the UDR replaced by an in-process sbi_core::http2::Server (TLS 1.3 + mTLS)
// that answers QueryAuthSubsData with a chosen status. The UDM is pointed at it through
// UDM_UDR_BASE_URL. Any other UDR failure keeps its 503 without a cause (ADR-0534).

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/json_body.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>

#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

constexpr unsigned short kStubUdrPort = 19781;
constexpr const char* kStubUdrInstanceId = "6c9e5f3a-1d2b-4c8e-9a10-0000000000d7";
constexpr const char* kSupi = "imsi-999700000000001";

// Same RAII rationale as test_udr_ondatachange_webhook.cpp: a joinable std::thread destructing
// without join() calls std::terminate().
class IoContextThread {
public:
    explicit IoContextThread(boost::asio::io_context& ioc) : ioc_(ioc) {
        thread_ = std::thread([&ioc] { ioc.run(); });
    }
    ~IoContextThread() {
        ioc_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    IoContextThread(const IoContextThread&) = delete;
    IoContextThread& operator=(const IoContextThread&) = delete;

private:
    boost::asio::io_context& ioc_;
    std::thread thread_;
};

sbi_core::http2::TlsConfig hello_nf_tls() {
    return sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
}

std::string fetch_udm_token() {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = std::string(nf_test::kTestNrfBase) + "/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body =
        "grant_type=client_credentials&nfInstanceId=" + std::string(nf_test::kTestAfInstanceId) +
        "&scope=nudm-ueau&targetNfType=UDM";
    auto resp = nf_test::send_token_request(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

sbi_core::http2::ClientResponse generate_auth_data(sbi_core::http2::Client& client) {
    sbi_core::http2::ClientResponse resp;
    std::string token;
    for (int attempt = 0; attempt < 100 && resp.status == 0; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (token.empty()) {
            token = fetch_udm_token();
        }
        if (token.empty()) {
            continue;
        }
        sbi_core::http2::ClientRequest req;
        req.method = "POST";
        req.url = "https://127.0.0.1:7780/nudm-ueau/v1/" + std::string(kSupi) +
                  "/security-information/generate-auth-data";
        req.headers.emplace("content-type", "application/json");
        req.headers.emplace("authorization", "Bearer " + token);
        req.body = json{{"servingNetworkName", "5G:mnc070.mcc999.3gppnetwork.org"},
                        {"ausfInstanceId", "00000000-0000-4000-8000-000000000ddd"}}
                       .dump();
        if (auto r = client.send(req); r.has_value()) {
            resp = *r;
        }
    }
    return resp;
}

std::string cause_of(const sbi_core::http2::ClientResponse& resp) {
    try {
        return json::parse(resp.body).value("cause", std::string());
    } catch (const json::parse_error&) {
        return "<no JSON body>";
    }
}

} // namespace

TEST(UdmUdrOverload, Udr503Or429IsBadGatewayInboundServerErrorOtherFailuresStay503) {
    std::atomic<int> udr_status{503};
    boost::asio::io_context stub_ioc;
    sbi_core::http2::Server stub_udr(stub_ioc, "127.0.0.1", kStubUdrPort, hello_nf_tls());
    stub_udr.add_route(
        "GET",
        "/nudr-dr/v2/subscription-data/{ueId}/authentication-data/authentication-subscription",
        [&udr_status](const sbi_core::http2::Request&) {
            return sbi_core::http2::problem_response(
                udr_status.load(), "Stub UDR", "stub UDR answer for this test", std::nullopt);
        });
    stub_udr.start();
    IoContextThread stub_thread(stub_ioc);

    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // The NRF issues a token towards UDRs only while one is registered (ADR-0499 phase 2).
    nf_test::register_test_nf(kStubUdrInstanceId, "UDR");

    ::setenv("UDM_UDR_BASE_URL", ("https://127.0.0.1:" + std::to_string(kStubUdrPort)).c_str(), 1);
    nf_test::SpawnedProcess udm(UDM_PATH);
    ::unsetenv("UDM_UDR_BASE_URL");
    ASSERT_GT(udm.pid(), 0) << "failed to fork udm";
    ASSERT_TRUE(nf_test::wait_tcp_listening(7780));

    sbi_core::http2::Client client(hello_nf_tls());

    for (const int inbound : {503, 429}) {
        udr_status = inbound;
        const auto resp = generate_auth_data(client);
        EXPECT_EQ(resp.status, 502) << "UDR answered " << inbound << ": " << resp.body;
        EXPECT_EQ(cause_of(resp), "INBOUND_SERVER_ERROR")
            << "UDR answered " << inbound << ": " << resp.body;
    }

    // Unchanged: any other unexpected UDR status is still the UDM's cause-less 503 (ADR-0534).
    udr_status = 500;
    const auto other = generate_auth_data(client);
    EXPECT_EQ(other.status, 503) << other.body;
    EXPECT_EQ(cause_of(other), "") << other.body;
}
