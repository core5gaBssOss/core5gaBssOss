// ADR-0551 increment 1 (P1, P2, P5): the PCF determines a V2X UE policy at
// Npcf_UEPolicyControl_Create / _Update and delivers the N2 PC5 policy to the AMF with
// Namf_Communication_N1N2MessageTransfer (TS 23.502 4.2.4, TS 23.287 6.2.2).
//
// Spawns the real NRF and PCF as separate processes. The UDM (Nudm_SDM v2x-data, TS 29.503) and the
// AMF (Namf_Communication, TS 29.518) are in-process sbi_core::http2::Server instances on the ports
// config/pcf.json names (udm_base_url 7780, amf_base_url 7778), over the same TLS 1.3 + mTLS stack
// every NF uses. The fake UDM answers a fixed body per SUPI; the fake AMF records what it receives.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/multipart.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;
using namespace std::chrono_literals;

constexpr const char* kAuthorizedSupi = "imsi-999700000000551";
constexpr const char* kNotAuthorizedSupi = "imsi-999700000000552";
constexpr const char* kPcfBase = "https://127.0.0.1:7783/npcf-ue-policy-control/v1";

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

// What the fake UDM and AMF saw. Every access holds `mutex`.
struct Recorder {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> udm_supis;
    std::vector<std::string> udm_auth_headers;
    struct AmfCall {
        std::string supi;
        std::string authorization;
        std::string content_type;
        std::string body;
    };
    std::vector<AmfCall> amf_calls;

    template <class Pred> bool wait_for(Pred pred, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, timeout, [&] { return pred(); });
    }
};

std::string header(const sbi_core::http2::Request& req, const std::string& name) {
    const auto it = req.headers.find(name);
    return it == req.headers.end() ? std::string{} : it->second;
}

json create_body(const std::string& supi, const std::string& supp_feat) {
    return json{{"notificationUri", "https://127.0.0.1:19551/pcf-ue-policy-notify"},
                {"supi", supi},
                {"suppFeat", supp_feat},
                {"pc5Capab", "NR_PC5"}};
}

sbi_core::http2::ClientRequest
pcf_post(const std::string& url, const std::string& token, const json& body) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = url;
    req.headers.emplace("content-type", "application/json");
    req.headers.emplace("authorization", "Bearer " + token);
    req.body = body.dump();
    return req;
}

// Checks one recorded N1N2MessageTransfer against TS 29.518 N1N2MessageTransferReqData with an
// N2InfoContainer of class V2X carrying n2Pc5Pol (PC5_QOS_PARA) and its NGAP binary part.
void expect_pc5_policy_transfer(const Recorder::AmfCall& call) {
    EXPECT_EQ(call.supi, kAuthorizedSupi);
    EXPECT_EQ(call.authorization.rfind("Bearer ", 0), 0u) << "no OAuth2 token on the AMF call";
    ASSERT_TRUE(sbi_core::multipart::is_multipart_related(call.content_type)) << call.content_type;
    const auto parts = sbi_core::multipart::parse(call.content_type, call.body);
    ASSERT_TRUE(parts.has_value()) << parts.error();
    ASSERT_EQ(parts->size(), 2u);
    EXPECT_EQ((*parts)[0].content_type, "application/json");
    const auto j = json::parse((*parts)[0].body);
    const auto& container = j.at("n2InfoContainer");
    EXPECT_EQ(container.at("n2InformationClass"), "V2X");
    const auto& pol = container.at("v2xInfo").at("n2Pc5Pol");
    EXPECT_EQ(pol.at("ngapIeType"), "PC5_QOS_PARA");
    const std::string content_id = pol.at("ngapData").at("contentId").get<std::string>();
    EXPECT_EQ((*parts)[1].content_type, "application/vnd.3gpp.ngap");
    ASSERT_TRUE((*parts)[1].content_id.has_value());
    EXPECT_EQ(*(*parts)[1].content_id, content_id);
    EXPECT_FALSE((*parts)[1].body.empty()) << "empty PC5QoSParameters";
}

} // namespace

TEST(PcfV2xProvisioning, CreateAndPlmnChangeSendTheN2Pc5PolicyOnlyForAnAuthorizedUe) {
    Recorder rec;

    boost::asio::io_context udm_ioc;
    sbi_core::http2::Server udm(udm_ioc, "127.0.0.1", 7780, test_tls());
    udm.add_route(
        "GET", "/nudm-sdm/v2/{supi}/v2x-data", [&rec](const sbi_core::http2::Request& req) {
            const std::string supi = req.path_params.at("supi");
            {
                std::lock_guard lock(rec.mutex);
                rec.udm_supis.push_back(supi);
                rec.udm_auth_headers.push_back(header(req, "authorization"));
            }
            rec.cv.notify_all();
            sbi_core::http2::Response r;
            r.status = 200;
            r.headers.emplace("content-type", "application/json");
            const char* auth = supi == kAuthorizedSupi ? "AUTHORIZED" : "NOT_AUTHORIZED";
            r.body = json{{"nrV2xServicesAuth", {{"vehicleUeAuth", auth}}}}.dump();
            return r;
        });
    udm.start();
    IoContextThread udm_thread(udm_ioc);

    boost::asio::io_context amf_ioc;
    sbi_core::http2::Server amf(amf_ioc, "127.0.0.1", 7778, test_tls());
    amf.add_route("POST",
                  "/namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages",
                  [&rec](const sbi_core::http2::Request& req) {
                      {
                          std::lock_guard lock(rec.mutex);
                          rec.amf_calls.push_back({req.path_params.at("ueContextId"),
                                                   header(req, "authorization"),
                                                   header(req, "content-type"),
                                                   req.body});
                      }
                      rec.cv.notify_all();
                      sbi_core::http2::Response r;
                      r.status = 200;
                      r.headers.emplace("content-type", "application/json");
                      r.body = json{{"cause", "N1_N2_TRANSFER_INITIATED"}}.dump();
                      return r;
                  });
    amf.start();
    IoContextThread amf_thread(amf_ioc);

    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0);
    std::this_thread::sleep_for(200ms);
    nf_test::SpawnedProcess pcf(PCF_PATH);
    ASSERT_GT(pcf.pid(), 0);

    sbi_core::http2::Client client(test_tls());
    ASSERT_TRUE(wait_reachable(client, std::string(kPcfBase) + "/policies/nonexistent", 50))
        << "pcf never became reachable";
    const std::string token = fetch_token("npcf-ue-policy-control");
    ASSERT_FALSE(token.empty());

    // P1 + P5: V2X (bit 4) and PlmnChange (bit 2) negotiated -> UDM consulted, AMF receives the
    // N2 PC5 policy.
    auto created = client.send(
        pcf_post(std::string(kPcfBase) + "/policies", token, create_body(kAuthorizedSupi, "A")));
    ASSERT_TRUE(created.has_value());
    ASSERT_EQ(created->status, 201) << created->body;
    EXPECT_EQ(json::parse(created->body).at("suppFeat"), "A")
        << "PCF must answer the intersection of V2X+PlmnChange with its own features";
    ASSERT_TRUE(rec.wait_for([&] { return rec.amf_calls.size() == 1; }, 5000ms))
        << "no N1N2MessageTransfer reached the AMF after Create";
    {
        std::lock_guard lock(rec.mutex);
        expect_pc5_policy_transfer(rec.amf_calls[0]);
        ASSERT_EQ(rec.udm_supis.size(), 1u);
        EXPECT_EQ(rec.udm_supis[0], kAuthorizedSupi);
        EXPECT_EQ(rec.udm_auth_headers[0].rfind("Bearer ", 0), 0u);
    }

    const auto loc = created->headers.find("location");
    ASSERT_NE(loc, created->headers.end());
    const std::string pol_id = loc->second.substr(loc->second.rfind('/') + 1);

    // P2: PLMN_CH with PlmnChange negotiated -> policy re-determined and re-sent.
    auto updated = client.send(pcf_post(std::string(kPcfBase) + "/policies/" + pol_id + "/update",
                                        token,
                                        json{{"triggers", json::array({"PLMN_CH"})}}));
    ASSERT_TRUE(updated.has_value());
    ASSERT_EQ(updated->status, 200) << updated->body;
    ASSERT_TRUE(rec.wait_for([&] { return rec.amf_calls.size() == 2; }, 5000ms))
        << "PLMN_CH did not re-send the N2 PC5 policy";
    {
        std::lock_guard lock(rec.mutex);
        expect_pc5_policy_transfer(rec.amf_calls[1]);
    }

    // Negative: the UDM says NOT_AUTHORIZED -> the UDM is asked, the AMF is not called.
    auto refused = client.send(
        pcf_post(std::string(kPcfBase) + "/policies", token, create_body(kNotAuthorizedSupi, "8")));
    ASSERT_TRUE(refused.has_value());
    ASSERT_EQ(refused->status, 201) << refused->body;
    ASSERT_TRUE(rec.wait_for(
        [&] {
            for (const auto& s : rec.udm_supis)
                if (s == kNotAuthorizedSupi)
                    return true;
            return false;
        },
        5000ms))
        << "the UDM was never asked about the not-authorized UE";
    std::this_thread::sleep_for(500ms);
    {
        std::lock_guard lock(rec.mutex);
        EXPECT_EQ(rec.amf_calls.size(), 2u) << "an N2 PC5 policy was sent for a not-authorized UE";
    }

    // Negative: V2X not negotiated -> neither the UDM nor the AMF is called.
    const std::size_t udm_before = [&] {
        std::lock_guard lock(rec.mutex);
        return rec.udm_supis.size();
    }();
    auto plain = client.send(
        pcf_post(std::string(kPcfBase) + "/policies", token, create_body(kAuthorizedSupi, "")));
    ASSERT_TRUE(plain.has_value());
    ASSERT_EQ(plain->status, 201) << plain->body;
    std::this_thread::sleep_for(1000ms);
    {
        std::lock_guard lock(rec.mutex);
        EXPECT_EQ(rec.udm_supis.size(), udm_before) << "UDM consulted without V2X negotiated";
        EXPECT_EQ(rec.amf_calls.size(), 2u);
    }
}
