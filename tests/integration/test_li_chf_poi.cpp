// Issue #10 (ADR-0488): the CHF IRI-POI's withheld-record tallies, the numbers behind
// chf_li_poi_records_withheld_total and the ChfLiPoiRecordsWithheld alert. Compiles
// nfs/chf/src/li_poi.cpp directly, as test_li_amf_poi.cpp does for the AMF's POI: a fake ADMF
// provisions the warrant over real LI_X1 (mTLS HTTP/2) with the ChargingDataEvents extension
// (TS 33.128 table 7.19.1-1), the test calls the POI the way the CHF's Nchf and Rf handlers do, and
// a loopback MDF2 (li_core::X2X3Server) shows whether a ChargingDataEvent was actually sent. No NF
// process: the counter logic is the POI's own. LiChfEndToEnd checks the same counter as the real
// chf process exports it on /metrics.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

#include "diameter_core/avp.hpp"
#include "li_core/x1.hpp"
#include "li_core/x2x3_pdu.hpp"
#include "li_core/x2x3_server.hpp"
#include "li_core/xiri.hpp"
#include "li_poi.hpp"

#include <gtest/gtest.h>

namespace {

using namespace std::chrono_literals;
using nlohmann::json;
using Reason = chf::ChfLiPoi::WithheldReason;
namespace x1 = li_core::x1;
namespace xiri = li_core::xiri;

// A fixed loopback port for the POI's LI_X1 listener (the sbi HTTP/2 server binds a fixed port),
// clear of every other LI test's; the fake MDF2 uses an ephemeral one.
constexpr std::uint16_t kX1Port = 19866;
constexpr const char* kX1Url = "https://127.0.0.1:19866/X1/NE";
constexpr const char* kNeId = "chf-poi-unit";
constexpr const char* kTargetImsi = "999700000000061";
constexpr const char* kOtherImsi = "999700000000062";

class Inbox {
public:
    void operator()(const li_core::Pdu& pdu, std::string_view) {
        const std::lock_guard<std::mutex> lock(mutex_);
        items_.push_back(pdu);
        cv_.notify_all();
    }
    bool wait_for(std::size_t n, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return items_.size() >= n; });
    }
    std::vector<li_core::Pdu> take() {
        const std::lock_guard<std::mutex> lock(mutex_);
        return items_;
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<li_core::Pdu> items_;
};

li_core::X2X3ServerConfig fake_mdf2_config() {
    li_core::X2X3ServerConfig cfg;
    cfg.bind_address = "127.0.0.1";
    cfg.port = 0; // ephemeral
    cfg.server_cert_path = CERTS_DIR "/chf/cert.pem";
    cfg.server_key_path = CERTS_DIR "/chf/key.pem";
    cfg.ca_path = CERTS_DIR "/ca/ca.crt";
    return cfg;
}

li_poi::Config poi_config(std::uint16_t mdf2_port) {
    li_poi::Config cfg;
    cfg.x1_bind_address = "127.0.0.1";
    cfg.x1_port = kX1Port;
    cfg.ne_identifier = kNeId;
    cfg.network_function_id = "chf-01.5gc.example.net";
    cfg.interception_point_id = "CHF-IRI-POI-1";
    cfg.mdf2_host = "127.0.0.1";
    cfg.mdf2_port = mdf2_port;
    cfg.mdf2_sni = "localhost"; // the lab chf cert's SAN
    cfg.cert_path = CERTS_DIR "/chf/cert.pem";
    cfg.key_path = CERTS_DIR "/chf/key.pem";
    cfg.ca_path = CERTS_DIR "/ca/ca.crt";
    return cfg;
}

sbi_core::http2::Client make_admf_client() {
    return sbi_core::http2::Client(sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    });
}

// ActivateTask for the target SUPI with the ChargingDataEvents extension set to `generated`.
std::string x1_activate(const std::string& xid, x1::ChargingDataEventsGenerated generated) {
    x1::TaskDetails task;
    task.xid = xid;
    task.targets.push_back({.kind = x1::TargetIdentifierKind::SupiImsi,
                            .element = "supiimsi",
                            .value = kTargetImsi,
                            .address = {}});
    task.charging_data_events = generated;
    task.delivery = x1::DeliveryType::X2Only;
    task.dids = {"44444444-4444-4444-8444-444444444444"};
    x1::Request request;
    request.header = {"admf-test",
                      kNeId,
                      "2026-10-20T00:00:00.000000Z",
                      "v1.23.1",
                      "00000000-0000-4000-8000-0000000000f6"};
    request.type = x1::MessageType::ActivateTask;
    request.body = x1::ActivateTask{task};
    const auto xml = x1::serialise_request({request});
    return xml.has_value() ? *xml : std::string();
}

// POST an X1 document to the POI's LI_X1 listener, retrying briefly while the listener comes up.
bool post_x1(sbi_core::http2::Client& admf, const std::string& body) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = kX1Url;
    req.headers.emplace("content-type", "application/xml");
    req.body = body;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (const auto r = admf.send(req); r.has_value()) {
            return r->status == 200 && r->body.find("ErrorResponse") == std::string::npos;
        }
        std::this_thread::sleep_for(100ms);
    }
    return false;
}

// A ChargingDataRequest (TS 32.291 6.1.6.2.1.1) as the SMF sends it for `imsi`, with PDU session
// charging information; `roamer` sets UserInformation.roamerInOut when non-empty. Empty is the
// home-network case: nfs/smf never sends roamerInOut.
std::string charging_data_request(const std::string& imsi, const std::string& roamer) {
    json user{{"servedGPSI", "msisdn-491701230061"}};
    if (!roamer.empty()) {
        user["roamerInOut"] = roamer;
    }
    return json{{"subscriberIdentifier", "imsi-" + imsi},
                {"nfConsumerIdentification", {{"nodeFunctionality", "SMF"}}},
                {"invocationTimeStamp", "2026-10-20T12:00:00Z"},
                {"invocationSequenceNumber", 0},
                {"pDUSessionChargingInformation",
                 {{"chargingId", 7},
                  {"userInformation", user},
                  {"pduSessionInformation",
                   {{"pduSessionID", 5},
                    {"dnnId", "internet"},
                    {"networkSlicingInfo", {{"sNSSAI", {{"sst", 1}}}}}}}}}}
        .dump();
}

// An Rf ACR as ChfLiPoi::report_rf_acr receives it: the octets are carried opaquely, the decoded
// AVPs only need User-Name (RFC 6733 code 1) naming the target -- the POI's first match key.
std::vector<diameter_core::Avp> acr_avps_for_target() {
    diameter_core::Avp user_name;
    user_name.code = 1;
    user_name.flags = diameter_core::AvpFlag::kMandatory;
    user_name.data.assign(kTargetImsi, kTargetImsi + std::char_traits<char>::length(kTargetImsi));
    return {user_name};
}

struct Lab {
    Inbox mdf2;
    li_core::X2X3Server mdf2_server{fake_mdf2_config(), std::ref(mdf2)};
};

std::size_t charging_data_events(const std::vector<li_core::Pdu>& pdus) {
    std::size_t n = 0;
    for (const auto& pdu : pdus) {
        if (const auto d = xiri::decode_xiri_payload(pdu.payload); d.has_value()) {
            n += std::holds_alternative<xiri::ChargingDataEvent>(d->event) ? 1U : 0U;
        }
    }
    return n;
}

} // namespace

// The ADR-0488 case: a home-network ChargingDataRequest for a target carries no roamerInOut, so the
// record is withheld -- and now counted, once, under MissingRoamerInOut and nothing else.
TEST(LiChfPoi, HomeNetworkRequestWithoutRoamerInOutIsCountedAsWithheld) {
    Lab lab;
    ASSERT_TRUE(lab.mdf2_server.start().has_value());
    chf::ChfLiPoi poi(poi_config(lab.mdf2_server.bound_port()));
    poi.start();
    auto admf = make_admf_client();
    ASSERT_TRUE(
        post_x1(admf,
                x1_activate("c0e1e2e3-e4e5-46e7-88e9-eaebecedeeef",
                            x1::ChargingDataEventsGenerated::DefaultChargingDataInformation)));

    EXPECT_EQ(poi.records_withheld(Reason::MissingRoamerInOut), 0U);
    poi.report_charging_data_request(charging_data_request(kTargetImsi, ""), /*offline=*/false);
    EXPECT_EQ(poi.records_withheld(Reason::MissingRoamerInOut), 1U);
    poi.report_charging_data_request(charging_data_request(kTargetImsi, ""), /*offline=*/true);
    EXPECT_EQ(poi.records_withheld(Reason::MissingRoamerInOut), 2U);
    EXPECT_EQ(poi.records_withheld(Reason::TranslatedNotBuilt), 0U);
    EXPECT_FALSE(lab.mdf2.wait_for(1, 2s)) << "a request without roamerInOut was reported";

    // A non-target's request owes no record to anyone: nothing withheld, nothing counted.
    poi.report_charging_data_request(charging_data_request(kOtherImsi, ""), /*offline=*/false);
    EXPECT_EQ(poi.records_withheld(Reason::MissingRoamerInOut), 2U);

    poi.stop();
    lab.mdf2_server.stop();
}

// A record that IS sent is not counted: roaming Nchf requests (OUT_BOUND converged, IN_BOUND
// offline) and an Rf ACR (no roamerInOut member, so never withheld for it) all reach the MDF2.
TEST(LiChfPoi, RecordsThatAreSentAreNotCountedAsWithheld) {
    Lab lab;
    ASSERT_TRUE(lab.mdf2_server.start().has_value());
    chf::ChfLiPoi poi(poi_config(lab.mdf2_server.bound_port()));
    poi.start();
    auto admf = make_admf_client();
    ASSERT_TRUE(
        post_x1(admf,
                x1_activate("c1e1e2e3-e4e5-46e7-88e9-eaebecedeeef",
                            x1::ChargingDataEventsGenerated::DefaultChargingDataInformation)));

    poi.report_charging_data_request(charging_data_request(kTargetImsi, "OUT_BOUND"),
                                     /*offline=*/false);
    poi.report_charging_data_request(charging_data_request(kTargetImsi, "IN_BOUND"),
                                     /*offline=*/true);
    poi.report_rf_acr({0x01, 0x00, 0x00, 0x14}, acr_avps_for_target());

    ASSERT_TRUE(lab.mdf2.wait_for(3, 10s)) << "expected three ChargingDataEvent xIRIs";
    EXPECT_EQ(charging_data_events(lab.mdf2.take()), 3U);
    EXPECT_EQ(poi.records_withheld(Reason::MissingRoamerInOut), 0U);
    EXPECT_EQ(poi.records_withheld(Reason::TranslatedNotBuilt), 0U);

    poi.stop();
    lab.mdf2_server.stop();
}

// The issue's second reason: a task asking for TranslatedChargingDataInformation gets no record
// (not built, ADR-0488) on Nchf or Rf -- counted under TranslatedNotBuilt, even when the request
// does carry roamerInOut, and never under MissingRoamerInOut.
TEST(LiChfPoi, TranslatedTaskIsCountedUnderItsOwnReason) {
    Lab lab;
    ASSERT_TRUE(lab.mdf2_server.start().has_value());
    chf::ChfLiPoi poi(poi_config(lab.mdf2_server.bound_port()));
    poi.start();
    auto admf = make_admf_client();
    ASSERT_TRUE(
        post_x1(admf,
                x1_activate("c2e1e2e3-e4e5-46e7-88e9-eaebecedeeef",
                            x1::ChargingDataEventsGenerated::TranslatedChargingDataInformation)));

    poi.report_charging_data_request(charging_data_request(kTargetImsi, "OUT_BOUND"),
                                     /*offline=*/false);
    poi.report_rf_acr({0x01, 0x00, 0x00, 0x14}, acr_avps_for_target());

    EXPECT_EQ(poi.records_withheld(Reason::TranslatedNotBuilt), 2U);
    EXPECT_EQ(poi.records_withheld(Reason::MissingRoamerInOut), 0U);
    EXPECT_FALSE(lab.mdf2.wait_for(1, 2s)) << "a Translated task got a record";

    poi.stop();
    lab.mdf2_server.stop();
}
