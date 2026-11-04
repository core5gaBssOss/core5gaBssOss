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

#include <algorithm>
#include <atomic>
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
    std::vector<json> update_notifies;    // PolicyUpdates that reached the moved notify URI (P7)
    std::size_t original_update_hits = 0; // UpdateNotify POSTs to the original notificationUri

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
// `uu == false` is the answer to a UE POLICY PROVISIONING REQUEST asking for PC5 only (P6a).
Bytes expected_command(std::uint8_t pti, bool uu = true) {
    Bytes contents;
    contents.push_back(0x01); // V2XP info type: PC5
    be16(contents, 3);
    append(contents, {0x01, 0x02, 0x03});
    if (uu) {
        contents.push_back(0x02); // V2XP info type: Uu
        be16(contents, 2);
        append(contents, {0x04, 0x05});
    }

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

    // The AMF answers a UPDP transfer with this status; 504 = UE not reachable (P7).
    std::atomic<int> amf_status{200};
    boost::asio::io_context amf_ioc;
    sbi_core::http2::Server amf(amf_ioc, "127.0.0.1", 7778, test_tls());
    amf.add_route(
        "POST",
        "/namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages",
        [&rec, &amf_status](const sbi_core::http2::Request& req) {
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
            if (updp && amf_status.load() == 504) {
                return json_response(504, json{{"status", 504}, {"cause", "UE_NOT_REACHABLE"}});
            }
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

    // The AMF's UE policy notification endpoint (notificationUri of the association). The original
    // URI redirects with 307 (TS 29.525 4.2.4.2); the moved one records the PolicyUpdate.
    boost::asio::io_context notify_ioc;
    sbi_core::http2::Server notify(notify_ioc, "127.0.0.1", 19551, test_tls());
    notify.add_route(
        "POST", "/pcf-ue-policy-notify/update", [&rec](const sbi_core::http2::Request&) {
            {
                std::lock_guard lock(rec.mutex);
                ++rec.original_update_hits;
            }
            sbi_core::http2::Response r;
            r.status = 307;
            r.headers.emplace("location", "https://127.0.0.1:19551/moved/update");
            return r;
        });
    notify.add_route("POST", "/moved/update", [&rec](const sbi_core::http2::Request& req) {
        {
            std::lock_guard lock(rec.mutex);
            rec.update_notifies.push_back(json::parse(req.body));
        }
        rec.cv.notify_all();
        return no_content();
    });
    notify.start();
    IoContextThread notify_thread(notify_ioc);

    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0);
    std::this_thread::sleep_for(200ms);
    // ADR-0554: the NRF refuses a token towards an NF type with no registered instance
    // (TS 33.501 13.4.1.1.2), so the stand-ins register like the real NFs would.
    nf_test::register_test_nf("00000000-0000-4000-8000-0000000080d1", "UDM");
    nf_test::register_test_nf("00000000-0000-4000-8000-0000000080e1", "UDR");
    nf_test::register_test_nf("00000000-0000-4000-8000-0000000080a1", "AMF");
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

    // P6a (ADR-0563): UE-requested V2X policy provisioning, TS 24.587 5.3.2. Each answer is one
    // more UPDP N1N2MessageTransfer; the PCF still has the command above pending, unanswered.
    std::uint8_t pending_pti = 0;
    {
        std::lock_guard lock(rec.mutex);
        pending_pti = check_updp_transfer(rec.updp_transfers[1]).at(0);
    }
    const auto request_answer = [&](const Bytes& request, std::size_t transfer_count) -> Bytes {
        auto r = client.send(n1_notify(pol_id, request));
        EXPECT_TRUE(r.has_value());
        if (r.has_value()) {
            EXPECT_EQ(r->status, 204) << r->body;
        }
        if (!rec.wait_for([&] { return rec.updp_transfers.size() == transfer_count; }, 5000ms)) {
            ADD_FAILURE() << "no answer to UE POLICY PROVISIONING REQUEST " << transfer_count;
            return {};
        }
        std::lock_guard lock(rec.mutex);
        return check_updp_transfer(rec.updp_transfers[transfer_count - 1]);
    };
    // The PTI of the pending network command: PROVISIONING REJECT #35 (TS 24.587 5.3.2.4).
    EXPECT_EQ(request_answer(Bytes{pending_pti, 0x05, 0x01, 0x01}, 3),
              (Bytes{pending_pti, 0x06, 35}));
    // V2XPC5I only (octet 3 bit 1): a COMMAND with the request's PTI and the PC5 info only.
    EXPECT_EQ(request_answer(Bytes{0x10, 0x05, 0x01, 0x01}, 4), expected_command(0x10, false));
    // A retransmission on T5040 expiry (5.3.2.6) is answered again with the same COMMAND.
    EXPECT_EQ(request_answer(Bytes{0x10, 0x05, 0x01, 0x01}, 5), expected_command(0x10, false));
    auto complete_req = client.send(n1_notify(pol_id, Bytes{0x10, 0x02}));
    ASSERT_TRUE(complete_req.has_value());
    EXPECT_EQ(complete_req->status, 204) << complete_req->body;
    // A2XI (octet 4 bit 2) on an association without A2X: #32 service option not supported.
    EXPECT_EQ(request_answer(Bytes{0x11, 0x05, 0x02, 0x00, 0x02}, 6), (Bytes{0x11, 0x06, 32}));
    // Requested UE policies IE claims 3 octets, carries 1: #96 invalid mandatory information.
    EXPECT_EQ(request_answer(Bytes{0x12, 0x05, 0x03, 0x01}, 7), (Bytes{0x12, 0x06, 96}));

    // P7 (ADR-0563): PCF-initiated UE Policy Association Modification, TS 23.502 4.16.12.2.
    // The AMF reports through n1n2FailureTxfNotifURI that the command was not delivered: the PCF
    // provisions CON_STATE_CH (UpdateNotify, followed through the 307) ...
    {
        auto bad = client.send(
            post_json(std::string(kCallbacks) + "/n1n2-failure/" + pol_id, R"({"cause":1})"));
        ASSERT_TRUE(bad.has_value());
        EXPECT_EQ(bad->status, 400) << "malformed N1N2MsgTxfrFailureNotification accepted";
        auto unknown =
            client.send(post_json(std::string(kCallbacks) + "/n1n2-failure/no-such-assoc",
                                  R"({"cause":"UE_NOT_RESPONDING"})"));
        ASSERT_TRUE(unknown.has_value());
        EXPECT_EQ(unknown->status, 404) << "failure notification for an unknown association";
    }
    auto failure = client.send(post_json(std::string(kCallbacks) + "/n1n2-failure/" + pol_id,
                                         json{{"cause", "UE_NOT_RESPONDING"},
                                              {"n1n2MsgDataUri",
                                               "https://127.0.0.1:7778/namf-comm/v1/ue-contexts/" +
                                                   std::string(kSupi) + "/n1-n2-messages/1"}}
                                             .dump()));
    ASSERT_TRUE(failure.has_value());
    EXPECT_EQ(failure->status, 204) << failure->body;
    ASSERT_TRUE(rec.wait_for([&] { return rec.update_notifies.size() == 1; }, 5000ms))
        << "no UpdateNotify (CON_STATE_CH) after the N1N2 transfer failure notification";
    {
        std::lock_guard lock(rec.mutex);
        EXPECT_EQ(rec.original_update_hits, 1u);
        const json& u = rec.update_notifies[0];
        const auto uri = u.at("resourceUri").get<std::string>();
        const std::string suffix = "/policies/" + pol_id;
        EXPECT_EQ(uri.substr(uri.size() - std::min(uri.size(), suffix.size())), suffix);
        EXPECT_NE(std::find(u.at("triggers").begin(), u.at("triggers").end(), "CON_STATE_CH"),
                  u.at("triggers").end())
            << u.dump();
    }
    // ... and re-delivers when the AMF reports the UE CONNECTED. The AMF still cannot reach it
    // (504): the PCF keeps the failure pending, and CON_STATE_CH is already provisioned, so no
    // second UpdateNotify.
    const auto connected = [&] {
        sbi_core::http2::ClientRequest u =
            post_json(std::string(kPcfBase) + "/policies/" + pol_id + "/update",
                      R"({"triggers":["CON_STATE_CH"],"connectState":"CONNECTED"})");
        u.headers.emplace("authorization", "Bearer " + token);
        return client.send(u);
    };
    amf_status = 504;
    auto up1 = connected();
    ASSERT_TRUE(up1.has_value());
    EXPECT_EQ(up1->status, 200) << up1->body;
    ASSERT_TRUE(rec.wait_for([&] { return rec.updp_transfers.size() == 8; }, 5000ms))
        << "CON_STATE_CH CONNECTED did not re-send the undelivered UE policy";
    std::this_thread::sleep_for(500ms);
    {
        std::lock_guard lock(rec.mutex);
        EXPECT_EQ(rec.update_notifies.size(), 1u) << "CON_STATE_CH provisioned twice";
        EXPECT_EQ(rec.original_update_hits, 1u);
    }
    // The UE is reachable now: a fresh MANAGE UE POLICY COMMAND goes out.
    amf_status = 200;
    auto up2 = connected();
    ASSERT_TRUE(up2.has_value());
    EXPECT_EQ(up2->status, 200) << up2->body;
    ASSERT_TRUE(rec.wait_for([&] { return rec.updp_transfers.size() == 9; }, 5000ms))
        << "the failure pending after the 504 was not re-delivered";
    {
        std::lock_guard lock(rec.mutex);
        const Bytes nas = check_updp_transfer(rec.updp_transfers[8]);
        ASSERT_FALSE(nas.empty());
        EXPECT_GE(nas[0], 0x80);
        EXPECT_LE(nas[0], 0xFE);
        EXPECT_EQ(nas, expected_command(nas[0]));
    }
    // A delivered command clears the flag: one more CONNECTED report sends nothing.
    auto up3 = connected();
    ASSERT_TRUE(up3.has_value());
    EXPECT_EQ(up3->status, 200) << up3->body;
    std::this_thread::sleep_for(500ms);
    {
        std::lock_guard lock(rec.mutex);
        EXPECT_EQ(rec.updp_transfers.size(), 9u) << "re-delivered without a pending failure";
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
