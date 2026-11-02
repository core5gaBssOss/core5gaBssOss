// ADR-0551 increment 2 (P3, P4), ADR-0555: the PCF reads the V2X service parameters an AF
// provisioned (UDR serviceParamData, TS 29.519), subscribes to their changes (subs-to-notify,
// dataInd SVC_PARAM), sends them to the UE in a MANAGE UE POLICY COMMAND (TS 24.501 Annex D) with
// Namf_Communication_N1N2MessageTransfer, N1 message class UPDP (TS 29.518), and takes the UE's
// answer from the N1MessageNotify it subscribed to (TS 23.502 4.2.4.3, TS 23.287 6.2.2).
//
// Spawns the real NRF and PCF as separate processes. The UDM (7780), UDR (7781) and AMF (7778) are
// in-process sbi_core::http2::Server instances on the ports config/pcf.json names, over the same
// TLS 1.3 + mTLS stack every NF uses. They are mocks: the expected NAS bytes below are built in
// this file from the TS 24.501 D.6 layout, independently of the PCF's encoder.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/multipart.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
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
using Bytes = std::vector<std::uint8_t>;

constexpr const char* kSupi = "imsi-999700000000553";
constexpr const char* kOtherSupi = "imsi-999700000000554";
constexpr const char* kPcfBase = "https://127.0.0.1:7783/npcf-ue-policy-control/v1";
constexpr const char* kCallbacks = "https://127.0.0.1:7783/npcf-ue-policy-control/v1/callbacks";
constexpr const char* kUdrSubsLocation =
    "https://127.0.0.1:7781/nudr-dr/v2/application-data/subs-to-notify/sub-553";

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

std::string header(const sbi_core::http2::Request& req, const std::string& name) {
    const auto it = req.headers.find(name);
    return it == req.headers.end() ? std::string{} : it->second;
}

sbi_core::http2::Response json_response(int status, const json& body) {
    sbi_core::http2::Response r;
    r.status = status;
    r.headers.emplace("content-type", "application/json");
    r.body = body.dump();
    return r;
}

sbi_core::http2::Response no_content() {
    sbi_core::http2::Response r;
    r.status = 204;
    return r;
}

// What the fake UDR and AMF saw. Every access holds `mutex`.
struct Recorder {
    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::string> udr_get_supis; // supis query of each serviceParamData GET
    std::vector<json> udr_subscriptions;    // subs-to-notify bodies
    std::vector<std::string> udr_deletes;   // subsId of each DELETE
    std::vector<json> amf_subscriptions;    // n1-n2-messages/subscriptions bodies
    std::vector<std::string> amf_sub_deletes;
    struct Transfer {
        std::string authorization;
        std::string content_type;
        std::string body;
    };
    std::vector<Transfer> updp_transfers; // N1N2MessageTransfer with an n1MessageContainer
    std::size_t n2_transfers = 0;         // increment 1's N2 PC5 policy transfers

    template <class Pred> bool wait_for(Pred pred, std::chrono::milliseconds timeout) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, timeout, [&] { return pred(); });
    }
};

void be16(Bytes& out, std::size_t v) {
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void append(Bytes& out, const Bytes& in) {
    out.insert(out.end(), in.begin(), in.end());
}

// MANAGE UE POLICY COMMAND for PLMN 999/70, UPSC 0x0001 and one V2XP part (TS 24.501 D.6.1,
// D.6.2: section management list -> sublist (PLMN ID) -> instruction (UPSC) -> UE policy part)
// carrying paramOverPc5 = 01 02 03 as V2XP info type 1 and paramOverUu = 04 05 as type 2.
Bytes expected_command(std::uint8_t pti) {
    Bytes contents;
    contents.push_back(0x01); // V2XP info type: PC5
    be16(contents, 3);
    append(contents, {0x01, 0x02, 0x03});
    contents.push_back(0x02); // V2XP info type: Uu
    be16(contents, 2);
    append(contents, {0x04, 0x05});

    Bytes part;
    be16(part, contents.size() + 1); // covers the part type octet (Table D.6.2.1 NOTE 2)
    part.push_back(0x03);            // UE policy part type V2XP
    append(part, contents);

    Bytes instruction;
    be16(instruction, 0x0001); // UPSC
    append(instruction, part);

    Bytes sublist{0x99, 0xF9, 0x07}; // PLMN ID 999/70
    be16(sublist, instruction.size());
    append(sublist, instruction);

    Bytes list;
    be16(list, sublist.size());
    append(list, sublist);

    Bytes msg{pti, 0x01}; // PTI, MANAGE UE POLICY COMMAND
    be16(msg, list.size());
    append(msg, list);
    return msg;
}

// Checks one UPDP N1N2MessageTransfer and returns its NAS bytes (empty on a failed check).
Bytes check_updp_transfer(const Recorder::Transfer& t) {
    EXPECT_EQ(t.authorization.rfind("Bearer ", 0), 0u) << "no OAuth2 token on the AMF call";
    EXPECT_TRUE(sbi_core::multipart::is_multipart_related(t.content_type)) << t.content_type;
    const auto parts = sbi_core::multipart::parse(t.content_type, t.body);
    if (!parts.has_value() || parts->size() != 2u) {
        ADD_FAILURE() << "UPDP transfer is not two multipart parts";
        return {};
    }
    EXPECT_EQ((*parts)[0].content_type, "application/json");
    const auto j = json::parse((*parts)[0].body);
    const auto& container = j.at("n1MessageContainer");
    EXPECT_EQ(container.at("n1MessageClass"), "UPDP");
    EXPECT_EQ(container.at("n1MessageContent").at("contentId"), "n1UePol");
    EXPECT_EQ((*parts)[1].content_type, "application/vnd.3gpp.5gnas");
    EXPECT_EQ((*parts)[1].content_id.value_or(""), "n1UePol");
    return Bytes((*parts)[1].body.begin(), (*parts)[1].body.end());
}

// N1MessageNotification (TS 29.518) carrying `nas` as the 5GS NAS binary part.
sbi_core::http2::ClientRequest n1_notify(const std::string& pol_id, const Bytes& nas) {
    const json root = {
        {"n1MessageContainer",
         {{"n1MessageClass", "UPDP"}, {"n1MessageContent", {{"contentId", "n1Msg"}}}}}};
    const auto enc = sbi_core::multipart::encode(
        {sbi_core::multipart::Part{
             .content_type = "application/json", .content_id = std::nullopt, .body = root.dump()},
         sbi_core::multipart::Part{.content_type = "application/vnd.3gpp.5gnas",
                                   .content_id = "n1Msg",
                                   .body = std::string(nas.begin(), nas.end())}});
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = std::string(kCallbacks) + "/n1-msg/" + pol_id;
    req.headers.emplace("content-type", enc.content_type_header);
    req.body = enc.body;
    return req;
}

sbi_core::http2::ClientRequest post_json(const std::string& url, const std::string& body) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = url;
    req.headers.emplace("content-type", "application/json");
    req.body = body;
    return req;
}

} // namespace

TEST(PcfUePolicyDelivery, CreateSendsTheV2xpCommandAndUdrChangesResendIt) {
    Recorder rec;

    boost::asio::io_context udm_ioc;
    sbi_core::http2::Server udm(udm_ioc, "127.0.0.1", 7780, test_tls());
    udm.add_route("GET", "/nudm-sdm/v2/{supi}/v2x-data", [](const sbi_core::http2::Request& req) {
        const char* auth = req.path_params.at("supi") == kSupi ? "AUTHORIZED" : "NOT_AUTHORIZED";
        return json_response(200, json{{"nrV2xServicesAuth", {{"vehicleUeAuth", auth}}}});
    });
    udm.start();
    IoContextThread udm_thread(udm_ioc);

    boost::asio::io_context udr_ioc;
    sbi_core::http2::Server udr(udr_ioc, "127.0.0.1", 7781, test_tls());
    udr.add_route(
        "GET",
        "/nudr-dr/v2/application-data/serviceParamData",
        [&rec](const sbi_core::http2::Request& req) {
            const auto q = req.query_params.find("supis");
            {
                std::lock_guard lock(rec.mutex);
                rec.udr_get_supis.push_back(q == req.query_params.end() ? "" : q->second);
            }
            rec.cv.notify_all();
            // The entry of another UE must be filtered out by the PCF.
            return json_response(
                200,
                json::array({{{"supi", kSupi}, {"paramOverPc5", "AQID"}, {"paramOverUu", "BAU="}},
                             {{"supi", kOtherSupi}, {"paramOverPc5", "/w=="}}}));
        });
    udr.add_route("POST",
                  "/nudr-dr/v2/application-data/subs-to-notify",
                  [&rec](const sbi_core::http2::Request& req) {
                      {
                          std::lock_guard lock(rec.mutex);
                          rec.udr_subscriptions.push_back(json::parse(req.body));
                      }
                      rec.cv.notify_all();
                      auto r = json_response(201, json::parse(req.body));
                      r.headers.emplace("location", kUdrSubsLocation);
                      return r;
                  });
    udr.add_route("DELETE",
                  "/nudr-dr/v2/application-data/subs-to-notify/{subsId}",
                  [&rec](const sbi_core::http2::Request& req) {
                      {
                          std::lock_guard lock(rec.mutex);
                          rec.udr_deletes.push_back(req.path_params.at("subsId"));
                      }
                      rec.cv.notify_all();
                      return no_content();
                  });
    udr.start();
    IoContextThread udr_thread(udr_ioc);

    boost::asio::io_context amf_ioc;
    sbi_core::http2::Server amf(amf_ioc, "127.0.0.1", 7778, test_tls());
    amf.add_route(
        "POST",
        "/namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages",
        [&rec](const sbi_core::http2::Request& req) {
            const auto ct = header(req, "content-type");
            const auto parts = sbi_core::multipart::parse(ct, req.body);
            const bool updp =
                parts.has_value() && !parts->empty() &&
                json::parse((*parts)[0].body, nullptr, false).contains("n1MessageContainer");
            {
                std::lock_guard lock(rec.mutex);
                if (updp) {
                    rec.updp_transfers.push_back({header(req, "authorization"), ct, req.body});
                } else {
                    ++rec.n2_transfers;
                }
            }
            rec.cv.notify_all();
            return json_response(200, json{{"cause", "N1_N2_TRANSFER_INITIATED"}});
        });
    amf.add_route("POST",
                  "/namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages/subscriptions",
                  [&rec](const sbi_core::http2::Request& req) {
                      {
                          std::lock_guard lock(rec.mutex);
                          rec.amf_subscriptions.push_back(json::parse(req.body));
                      }
                      rec.cv.notify_all();
                      return json_response(201, json{{"n1n2NotifySubscriptionId", "s1"}});
                  });
    amf.add_route(
        "DELETE",
        "/namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages/subscriptions/{subscriptionId}",
        [&rec](const sbi_core::http2::Request& req) {
            {
                std::lock_guard lock(rec.mutex);
                rec.amf_sub_deletes.push_back(req.path_params.at("subscriptionId"));
            }
            rec.cv.notify_all();
            return no_content();
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

    // Create with V2X (feature 4) negotiated for an authorized UE.
    sbi_core::http2::ClientRequest create;
    create.method = "POST";
    create.url = std::string(kPcfBase) + "/policies";
    create.headers.emplace("content-type", "application/json");
    create.headers.emplace("authorization", "Bearer " + token);
    create.body = json{
        {"notificationUri", "https://127.0.0.1:19551/pcf-ue-policy-notify"},
        {"supi", kSupi},
        {"suppFeat", "8"},
        {"pc5Capab",
         "NR_PC5"}}.dump();
    auto created = client.send(create);
    ASSERT_TRUE(created.has_value());
    ASSERT_EQ(created->status, 201) << created->body;
    const auto loc = created->headers.find("location");
    ASSERT_NE(loc, created->headers.end());
    const std::string pol_id = loc->second.substr(loc->second.rfind('/') + 1);

    // P3 + P4: both subscriptions and one UPDP transfer.
    ASSERT_TRUE(rec.wait_for(
        [&] {
            return !rec.updp_transfers.empty() && !rec.udr_subscriptions.empty() &&
                   !rec.amf_subscriptions.empty();
        },
        5000ms))
        << "no UPDP N1N2MessageTransfer / subscriptions after Create";

    std::uint8_t first_pti = 0;
    {
        std::lock_guard lock(rec.mutex);
        ASSERT_EQ(rec.udr_get_supis.size(), 1u);
        EXPECT_EQ(rec.udr_get_supis[0], kSupi);

        const json& us = rec.udr_subscriptions[0];
        EXPECT_EQ(us.at("notificationUri"), std::string(kCallbacks) + "/udr-app-data");
        ASSERT_EQ(us.at("dataFilters").size(), 1u);
        EXPECT_EQ(us.at("dataFilters")[0].at("dataInd"), "SVC_PARAM");
        EXPECT_EQ(us.at("dataFilters")[0].at("supis"), json::array({kSupi}));

        const json& as = rec.amf_subscriptions[0];
        EXPECT_EQ(as.at("n1MessageClass"), "UPDP");
        EXPECT_EQ(as.at("n1NotifyCallbackUri"), std::string(kCallbacks) + "/n1-msg/" + pol_id);
        EXPECT_TRUE(as.contains("nfId"));

        ASSERT_EQ(rec.updp_transfers.size(), 1u);
        const Bytes nas = check_updp_transfer(rec.updp_transfers[0]);
        ASSERT_FALSE(nas.empty());
        first_pti = nas[0];
        EXPECT_GE(first_pti, 0x80);
        EXPECT_LE(first_pti, 0xFE);
        EXPECT_EQ(nas, expected_command(first_pti))
            << "the other UE's serviceParamData entry must not be sent";
    }

    // N1MessageNotify: malformed bodies are refused with 400.
    {
        auto plain = client.send(post_json(std::string(kCallbacks) + "/n1-msg/" + pol_id, "{}"));
        ASSERT_TRUE(plain.has_value());
        EXPECT_EQ(plain->status, 400) << "non-multipart N1MessageNotification accepted";
        auto short_nas = client.send(n1_notify(pol_id, Bytes{first_pti}));
        ASSERT_TRUE(short_nas.has_value());
        EXPECT_EQ(short_nas->status, 400) << "1-octet UE policy message accepted";
    }
    // The UE's MANAGE UE POLICY COMPLETE for the pending PTI.
    auto complete = client.send(n1_notify(pol_id, Bytes{first_pti, 0x02}));
    ASSERT_TRUE(complete.has_value());
    EXPECT_EQ(complete->status, 204) << complete->body;

    // UDR change notification: malformed -> 400; for this UE -> 204 and a fresh command.
    {
        auto bad = client.send(post_json(std::string(kCallbacks) + "/udr-app-data", "{}"));
        ASSERT_TRUE(bad.has_value());
        EXPECT_EQ(bad->status, 400) << "non-array ApplicationDataChangeNotif accepted";
    }
    auto notified =
        client.send(post_json(std::string(kCallbacks) + "/udr-app-data",
                              json::array({{{"serParamData", {{"supi", kSupi}}}}}).dump()));
    ASSERT_TRUE(notified.has_value());
    EXPECT_EQ(notified->status, 204) << notified->body;
    ASSERT_TRUE(rec.wait_for([&] { return rec.updp_transfers.size() == 2; }, 5000ms))
        << "UDR change notification did not re-send the UE policy";
    {
        std::lock_guard lock(rec.mutex);
        EXPECT_EQ(rec.udr_get_supis.size(), 2u);
        const Bytes nas = check_updp_transfer(rec.updp_transfers[1]);
        ASSERT_FALSE(nas.empty());
        EXPECT_GE(nas[0], 0x80);
        EXPECT_LE(nas[0], 0xFE);
        EXPECT_EQ(nas, expected_command(nas[0]));
        EXPECT_EQ(rec.udr_subscriptions.size(), 1u) << "re-delivery must not re-subscribe";
        EXPECT_EQ(rec.amf_subscriptions.size(), 1u);
    }

    // Delete: both subscriptions are removed.
    sbi_core::http2::ClientRequest del;
    del.method = "DELETE";
    del.url = std::string(kPcfBase) + "/policies/" + pol_id;
    del.headers.emplace("authorization", "Bearer " + token);
    auto deleted = client.send(del);
    ASSERT_TRUE(deleted.has_value());
    EXPECT_EQ(deleted->status, 204) << deleted->body;
    ASSERT_TRUE(rec.wait_for(
        [&] { return !rec.udr_deletes.empty() && !rec.amf_sub_deletes.empty(); }, 5000ms))
        << "subscriptions not removed after Delete";
    {
        std::lock_guard lock(rec.mutex);
        EXPECT_EQ(rec.udr_deletes[0], "sub-553");
        EXPECT_EQ(rec.amf_sub_deletes[0], "s1");
    }
}
