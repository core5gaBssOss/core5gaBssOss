// ADR-0641 (ADR-0570 class, PCF part): a SUPI taken from a request body reaches only its own
// resource at the UDR.
//
// - SmPolicyCreate (TS 29.512 Npcf_SMPolicyControl): the PCF reads the subscriber's SmPolicyData
//   at `GET {apiRoot}/nudr-dr/v2/policy-data/ues/{ueId}/sm-data` (TS29519_Policy_Data.yaml). The
//   SUPI is a path segment there, so it is encoded with `encode_path_segment`.
// - UE policy delivery (ADR-0555): the PCF reads V2X service parameters at
//   `GET {apiRoot}/nudr-dr/v2/application-data/serviceParamData?supis=...`
//   (TS29519_Application_Data.yaml, query parameter `supis`). The SUPI is a query value there, so
//   it is encoded with `encode_query_value`; the path-segment encoder leaves "&" and "=" alone.
//
// Spawns the real NRF and PCF. The UDR (7781), UDM (7780) and AMF (7778) are in-process stand-ins
// that record what they receive.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;
using namespace std::chrono_literals;

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

sbi_core::http2::TlsConfig test_tls() {
    return sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
        .verify = {},
    };
}

bool wait_reachable(sbi_core::http2::Client& client, const std::string& url, int max_attempts) {
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = url;
        if (client.send(req).has_value()) {
            return true;
        }
        std::this_thread::sleep_for(100ms);
    }
    return false;
}

std::string fetch_token(const std::string& scope) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body =
        "grant_type=client_credentials&nfInstanceId=" + std::string(nf_test::kTestAfInstanceId) +
        "&scope=" + scope + "&targetNfType=PCF";
    auto resp = nf_test::send_token_request(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

sbi_core::http2::Response json_response(int status, const json& body) {
    sbi_core::http2::Response r;
    r.status = status;
    r.headers.emplace("content-type", "application/json");
    r.body = body.dump();
    return r;
}

// What the stand-ins saw. Every access holds `mutex`.
struct Recorder {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> values;

    void add(std::string v) {
        {
            std::lock_guard lock(mutex);
            values.push_back(std::move(v));
        }
        cv.notify_all();
    }
    bool wait_nonempty(std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, timeout, [&] { return !values.empty(); });
    }
};

} // namespace

TEST(PcfPeerUriEncoding, SmPolicySupiWithQueryCharacterReadsOnlyItsOwnSmData) {
    // Without the encoding, "?" ends the path: the UDR is asked for
    // /policy-data/ues/imsi-999700000000641 with the query "x/sm-data", which is not the sm-data
    // resource of this SUPI.
    const std::string supi = "imsi-999700000000641?x";
    Recorder sm_data_ue_ids;

    boost::asio::io_context udr_ioc;
    sbi_core::http2::Server udr(udr_ioc, "127.0.0.1", 7781, test_tls());
    udr.add_route("GET",
                  "/nudr-dr/v2/policy-data/ues/{ueId}/sm-data",
                  [&sm_data_ue_ids](const sbi_core::http2::Request& req) {
                      sm_data_ue_ids.add(req.path_params.at("ueId"));
                      return json_response(200, json::object());
                  });
    udr.start();
    IoContextThread udr_thread(udr_ioc);

    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    std::this_thread::sleep_for(200ms);
    // ADR-0554: the NRF issues no token towards an NF type with no registered instance.
    nf_test::register_test_nf("00000000-0000-4000-8000-0000000064a1", "UDR");
    nf_test::SpawnedProcess pcf(PCF_PATH);
    ASSERT_GT(pcf.pid(), 0) << "failed to fork pcf";

    sbi_core::http2::Client client(test_tls());
    ASSERT_TRUE(wait_reachable(
        client, "https://127.0.0.1:7783/npcf-am-policy-control/v1/policies/nonexistent", 50))
        << "pcf never became reachable";
    const std::string token = fetch_token("npcf-smpolicycontrol");
    ASSERT_FALSE(token.empty());

    sbi_core::http2::ClientRequest create;
    create.method = "POST";
    create.url = "https://127.0.0.1:7783/npcf-smpolicycontrol/v1/sm-policies";
    create.headers.emplace("content-type", "application/json");
    create.headers.emplace("authorization", "Bearer " + token);
    create.body = json{{"supi", supi},
                       {"pduSessionId", 5},
                       {"pduSessionType", "IPV4"},
                       {"dnn", "internet"},
                       {"notificationUri", "https://127.0.0.1:19641/sm-policy-notify"},
                       {"sliceInfo", {{"sst", 1}}}}
                      .dump();
    auto created = client.send(create);
    ASSERT_TRUE(created.has_value());
    ASSERT_EQ(created->status, 201) << created->body;

    ASSERT_TRUE(sm_data_ue_ids.wait_nonempty(5000ms))
        << "the PCF never read this SUPI's sm-data resource at the UDR";
    std::lock_guard lock(sm_data_ue_ids.mutex);
    ASSERT_EQ(sm_data_ue_ids.values.size(), 1u);
    EXPECT_EQ(sm_data_ue_ids.values[0], supi);
}

TEST(PcfPeerUriEncoding, UePolicySupiWithAmpersandIsOneServiceParamDataQueryValue) {
    // Without the query-value encoding, "&" starts another parameter: the UDR sees
    // supis=imsi-999700000000641 and a second supis=imsi-999700000000642, i.e. another UE's data.
    const std::string supi = "imsi-999700000000641&supis=imsi-999700000000642";
    Recorder supis_values;

    boost::asio::io_context udm_ioc;
    sbi_core::http2::Server udm(udm_ioc, "127.0.0.1", 7780, test_tls());
    udm.add_route("GET", "/nudm-sdm/v2/{supi}/v2x-data", [](const sbi_core::http2::Request&) {
        return json_response(200, json{{"nrV2xServicesAuth", {{"vehicleUeAuth", "AUTHORIZED"}}}});
    });
    udm.start();
    IoContextThread udm_thread(udm_ioc);

    boost::asio::io_context udr_ioc;
    sbi_core::http2::Server udr(udr_ioc, "127.0.0.1", 7781, test_tls());
    udr.add_route("GET",
                  "/nudr-dr/v2/application-data/serviceParamData",
                  [&supis_values](const sbi_core::http2::Request& req) {
                      const auto q = req.query_params.find("supis");
                      supis_values.add(q == req.query_params.end() ? "" : q->second);
                      return json_response(200, json::array());
                  });
    udr.start();
    IoContextThread udr_thread(udr_ioc);

    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    std::this_thread::sleep_for(200ms);
    nf_test::register_test_nf("00000000-0000-4000-8000-0000000064d1", "UDM");
    nf_test::register_test_nf("00000000-0000-4000-8000-0000000064e1", "UDR");
    nf_test::register_test_nf("00000000-0000-4000-8000-0000000064b1", "AMF");
    nf_test::SpawnedProcess pcf(PCF_PATH);
    ASSERT_GT(pcf.pid(), 0) << "failed to fork pcf";

    sbi_core::http2::Client client(test_tls());
    ASSERT_TRUE(wait_reachable(
        client, "https://127.0.0.1:7783/npcf-ue-policy-control/v1/policies/nonexistent", 50))
        << "pcf never became reachable";
    const std::string token = fetch_token("npcf-ue-policy-control");
    ASSERT_FALSE(token.empty());

    // V2X (feature 4, suppFeat "8") negotiated, so the PCF fetches the service parameters.
    sbi_core::http2::ClientRequest create;
    create.method = "POST";
    create.url = "https://127.0.0.1:7783/npcf-ue-policy-control/v1/policies";
    create.headers.emplace("content-type", "application/json");
    create.headers.emplace("authorization", "Bearer " + token);
    create.body = json{
        {"notificationUri", "https://127.0.0.1:19641/pcf-ue-policy-notify"},
        {"supi", supi},
        {"suppFeat", "8"},
        {"pc5Capab",
         "NR_PC5"}}.dump();
    auto created = client.send(create);
    ASSERT_TRUE(created.has_value());
    ASSERT_EQ(created->status, 201) << created->body;

    ASSERT_TRUE(supis_values.wait_nonempty(5000ms))
        << "the PCF never asked the UDR for serviceParamData";
    std::lock_guard lock(supis_values.mutex);
    ASSERT_EQ(supis_values.values.size(), 1u);
    EXPECT_EQ(supis_values.values[0], supi);
}
