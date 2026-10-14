// ADR-0491: two real SEPP processes -- PLMN A (001/01) and PLMN B (001/02) -- negotiate N32-c with
// TLS security (TS 29.573 5.2.2), and an NF service consumer in PLMN A reaches the real NRF of
// PLMN B through both of them over N32-f (5.3.3, TS 29.500 6.1.4.3.3/6.1.4.3.4), addressing it by
// its inter-PLMN FQDN in 3gpp-Sbi-Target-apiRoot. The NRF's Nnrf_Bootstrapping (unauthenticated
// by design) is the target. Nothing is mocked: the test is only the consumer and a direct N32-f
// client used to show a request without an N32-c context is refused.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>
#include <string>
#include <thread>

#include "spawn_guard.hpp"

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;
using nlohmann::json;

constexpr const char* kSeppA = "sepp.5gc.mnc001.mcc001.3gppnetwork.org";
constexpr const char* kSeppB = "sepp.5gc.mnc002.mcc001.3gppnetwork.org";
constexpr const char* kNrfB = "https://nrf.5gc.mnc002.mcc001.3gppnetwork.org:7777";

sbi_core::http2::TlsConfig tls(const char* nf) {
    return sbi_core::http2::TlsConfig{
        .cert_path = std::string(CERTS_DIR) + "/" + nf + "/cert.pem",
        .key_path = std::string(CERTS_DIR) + "/" + nf + "/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
}

// PLMN A's SEPP is config/sepp.json as checked in; PLMN B's is derived from it.
std::string write_config_b() {
    std::ifstream in(SEPP_CONFIG_TEMPLATE);
    json cfg = json::parse(in);
    cfg["port"] = 7822;
    cfg["n32_port"] = 7823;
    cfg["metrics_bind_address"] = "0.0.0.0:9495";
    cfg["fqdn"] = kSeppB;
    cfg["mnc"] = "02";
    cfg["peers"] = json::array({json{{"fqdn", kSeppA},
                                     {"mcc", "001"},
                                     {"mnc", "01"},
                                     {"n32_api_root", "https://127.0.0.1:7821"},
                                     {"initiate", false}}});
    cfg["local_nf_routes"] =
        json{{"nrf.5gc.mnc002.mcc001.3gppnetwork.org:7777", "https://127.0.0.1:7777"}};
    const std::string path = std::string(::testing::TempDir()) + "sepp_b.json";
    std::ofstream(path) << cfg.dump(2);
    return path;
}

sbi_core::http2::ClientResponse send_retrying(sbi_core::http2::Client& c,
                                              const sbi_core::http2::ClientRequest& req,
                                              int want_status,
                                              std::chrono::seconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    sbi_core::http2::ClientResponse last;
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto r = c.send(req); r.has_value()) {
            last = *r;
            if (r->status == want_status) {
                return last;
            }
        }
        std::this_thread::sleep_for(200ms);
    }
    return last;
}

} // namespace

TEST(SeppEndToEnd, AConsumerInPlmnAReachesTheNrfOfPlmnBThroughBothSepps) {
    nf_test::SpawnedProcess nrf{NRF_PATH};
    ::setenv("SEPP_CONFIG_FILE", SEPP_CONFIG_TEMPLATE, 1);
    nf_test::SpawnedProcess sepp_a{SEPP_PATH};
    const auto cfg_b = write_config_b();
    ::setenv("SEPP_CONFIG_FILE", cfg_b.c_str(), 1);
    nf_test::SpawnedProcess sepp_b{SEPP_PATH};
    ::unsetenv("SEPP_CONFIG_FILE");

    // The consumer (an NF of PLMN A) sends to its own SEPP with the target apiRoot in the header.
    sbi_core::http2::Client consumer(tls("hello-nf"));
    sbi_core::http2::ClientRequest req;
    req.method = "GET";
    req.url = "https://127.0.0.1:7820/bootstrapping";
    req.headers.emplace("3gpp-sbi-target-apiroot", kNrfB);
    // 200 only once SEPP A has negotiated with SEPP B (until then: 504, no N32 context).
    const auto r = send_retrying(consumer, req, 200, 30s);
    ASSERT_EQ(r.status, 200) << r.body;
    const auto body = json::parse(r.body);
    EXPECT_TRUE(body.contains("_links")) << r.body; // the NRF's BootstrappingInfo
    // Both SEPPs relayed the response (TS 29.500 6.10.10.3).
    const auto via = r.headers.find("via");
    ASSERT_NE(via, r.headers.end());
    EXPECT_NE(via->second.find(std::string("SEPP-") + kSeppB), std::string::npos) << via->second;
    EXPECT_NE(via->second.find(std::string("SEPP-") + kSeppA), std::string::npos) << via->second;

    // A target in a PLMN with no N32 context: 504 TARGET_NF_NOT_REACHABLE, originated by SEPP A.
    sbi_core::http2::ClientRequest unknown = req;
    unknown.headers.erase("3gpp-sbi-target-apiroot");
    unknown.headers.emplace("3gpp-sbi-target-apiroot",
                            "https://nrf.5gc.mnc009.mcc001.3gppnetwork.org:7777");
    const auto u = consumer.send(unknown);
    ASSERT_TRUE(u.has_value());
    EXPECT_EQ(u->status, 504);
    EXPECT_EQ(json::parse(u->body).value("cause", ""), "TARGET_NF_NOT_REACHABLE");
    const auto server = u->headers.find("server");
    ASSERT_NE(server, u->headers.end());
    EXPECT_EQ(server->second, std::string("SEPP-") + kSeppA);
}

TEST(SeppEndToEnd, AnN32fRequestFromAPeerWithoutAContextIsRefused) {
    nf_test::SpawnedProcess nrf{NRF_PATH};
    const auto cfg_b = write_config_b();
    ::setenv("SEPP_CONFIG_FILE", cfg_b.c_str(), 1);
    nf_test::SpawnedProcess sepp_b{SEPP_PATH};
    ::unsetenv("SEPP_CONFIG_FILE");

    // No SEPP A runs, so SEPP B holds no N32-c context. A client presenting the SEPP certificate
    // and claiming to be SEPP A goes straight to B's N32-f.
    sbi_core::http2::Client peer(tls("sepp"));
    sbi_core::http2::ClientRequest req;
    req.method = "GET";
    req.url = "https://127.0.0.1:7823/bootstrapping";
    req.headers.emplace("3gpp-sbi-target-apiroot", kNrfB);
    req.headers.emplace("via", std::string("2.0 SEPP-") + kSeppA);
    const auto r = send_retrying(peer, req, 403, 20s);
    ASSERT_EQ(r.status, 403) << r.body;
    EXPECT_EQ(json::parse(r.body).value("cause", ""), "CONTEXT_NOT_FOUND");

    // An exchange-capability from an unconfigured SEPP is refused NEGOTIATION_NOT_ALLOWED.
    sbi_core::http2::ClientRequest neg;
    neg.method = "POST";
    neg.url = "https://127.0.0.1:7823/n32c-handshake/v1/exchange-capability";
    neg.headers.emplace("content-type", "application/json");
    neg.body = json{{"sender", "sepp.5gc.mnc009.mcc001.3gppnetwork.org"},
                    {"supportedSecCapabilityList", json::array({"TLS"})}}
                   .dump();
    const auto n = peer.send(neg);
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(n->status, 403);
    EXPECT_EQ(json::parse(n->body).value("cause", ""), "NEGOTIATION_NOT_ALLOWED");
}
