// REVIEW-5ae0ef1 SPEC-02/03/04: PATCH bodies are applied as their YAML defines them. Real nrf + NF
// processes.
// - NEF Monitoring Event (TS29122_MonitoringEvent.yaml): application/json-patch+json, PatchItem[].
// - NEF 5GLAN (TS29522_5GLANParameterProvision.yaml): a 5GLanParametersProvisionPatch wrapper whose
//   5gLanParamsPatch is merged into the stored 5gLanParams (TS 29.522 table 5.7.2.3.5-1).
// - NSACF slice EE (TS29536_Nnsacf_SliceEventExposure.yaml): application/json-patch+json, 200 is a
//   CreatedSACEventSubscription.

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

sbi_core::http2::Client make_client() {
    return sbi_core::http2::Client(sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    });
}

sbi_core::http2::ClientResponse send(sbi_core::http2::Client& c,
                                     const std::string& method,
                                     const std::string& url,
                                     const std::string& content_type = "",
                                     const std::string& body = "") {
    sbi_core::http2::ClientRequest req;
    req.method = method;
    req.url = url;
    if (!content_type.empty()) {
        req.headers.emplace("content-type", content_type);
    }
    req.body = body;
    auto r = c.send(req);
    return r.has_value() ? *r : sbi_core::http2::ClientResponse{};
}

// Waits for the NF to answer at all (any status), up to ~10 s.
void wait_up(sbi_core::http2::Client& c, const std::string& url) {
    for (int i = 0; i < 100 && send(c, "GET", url).status == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

std::string id_from_location(const sbi_core::http2::ClientResponse& r) {
    const auto it = r.headers.find("location");
    return it == r.headers.end() ? "" : it->second.substr(it->second.rfind('/') + 1);
}

constexpr const char* kJsonPatch = "application/json-patch+json";

} // namespace

TEST(PatchConformance, NefMonitoringEventAppliesAJsonPatchAndRejectsAMergePatch) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0);
    nf_test::SpawnedProcess nef(NEF_PATH);
    ASSERT_GT(nef.pid(), 0);
    auto c = make_client();
    nf_test::attach_test_tokens(c);
    const std::string base =
        "https://127.0.0.1:7790/3gpp-monitoring-event/v1/af-patch/subscriptions";
    wait_up(c, base);

    // A group subscription: stored at the NEF without a UDM (no single UE to broker for).
    const auto created = send(c,
                              "POST",
                              base,
                              "application/json",
                              json{{"notificationDestination", "https://127.0.0.1:9/notify"},
                                   {"monitoringType", "LOCATION_REPORTING"},
                                   {"maximumNumberOfReports", 1},
                                   {"externalGroupId", "group-1@example.com"}}
                                  .dump());
    ASSERT_EQ(created.status, 201) << created.body;
    const auto url = base + "/" + id_from_location(created);

    auto r = send(c,
                  "PATCH",
                  url,
                  kJsonPatch,
                  R"([{"op":"replace","path":"/maximumNumberOfReports","value":5}])");
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(json::parse(r.body).value("maximumNumberOfReports", 0), 5) << r.body;
    EXPECT_EQ(json::parse(r.body).value("monitoringType", ""), "LOCATION_REPORTING");

    r = send(c, "PATCH", url, kJsonPatch, R"({"maximumNumberOfReports":7})");
    EXPECT_EQ(r.status, 400) << "a merge-patch object is not a PatchItem array: " << r.body;
    r = send(c,
             "PATCH",
             url,
             "application/merge-patch+json",
             R"([{"op":"replace","path":"/maximumNumberOfReports","value":7}])");
    EXPECT_EQ(r.status, 415) << r.body;

    r = send(c, "GET", url);
    ASSERT_EQ(r.status, 200) << r.body;
    EXPECT_EQ(json::parse(r.body).value("maximumNumberOfReports", 0), 5)
        << "the rejected patches changed nothing: " << r.body;
}

TEST(PatchConformance, Nef5glanPatchMergesIntoTheStored5gLanParams) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0);
    nf_test::SpawnedProcess nef(NEF_PATH);
    ASSERT_GT(nef.pid(), 0);
    auto c = make_client();
    nf_test::attach_test_tokens(c);
    const std::string base = "https://127.0.0.1:7790/3gpp-5glan-pp/v1/af-patch/subscriptions";
    wait_up(c, base);

    const json params = {
        {"exterGroupId", "lan-1@example.com"},
        {"gpsis", {{"1", "msisdn-12345678901"}}},
        {"dnn", "internet"},
        {"snssai", {{"sst", 1}}},
        {"sessionType", "IPV4"},
        {"appDesps",
         {{"1",
           {{"osId", "9f4bc6b4-0000-4000-8000-000000000001"}, {"appIds", {{"1", "app-1"}}}}}}}};
    const auto created = send(c,
                              "POST",
                              base,
                              "application/json",
                              json{{"5gLanParams", params}, {"suppFeat", "0"}}.dump());
    ASSERT_EQ(created.status, 201) << created.body;
    const auto url = base + "/" + id_from_location(created);

    auto r = send(c,
                  "PATCH",
                  url,
                  "application/merge-patch+json",
                  json{{"5gLanParamsPatch", {{"gpsis", {{"2", "msisdn-12345678902"}}}}}}.dump());
    ASSERT_EQ(r.status, 200) << r.body;

    r = send(c, "GET", url);
    ASSERT_EQ(r.status, 200) << r.body;
    const auto doc = json::parse(r.body);
    EXPECT_FALSE(doc.contains("5gLanParamsPatch"))
        << "the wrapper member is not stored: " << r.body;
    ASSERT_TRUE(doc.contains("5gLanParams")) << r.body;
    EXPECT_EQ(doc["5gLanParams"]["gpsis"].value("1", ""), "msisdn-12345678901") << r.body;
    EXPECT_EQ(doc["5gLanParams"]["gpsis"].value("2", ""), "msisdn-12345678902") << r.body;
}

TEST(PatchConformance, NsacfSliceEePatchIsAJsonPatchAnsweredWithCreatedSacEventSubscription) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0);
    nf_test::SpawnedProcess nsacf(NSACF_PATH);
    ASSERT_GT(nsacf.pid(), 0);
    auto c = make_client();
    nf_test::attach_test_tokens(c);
    const std::string base = "https://127.0.0.1:7797/nnsacf-slice-ee/v1/subscriptions";
    wait_up(c, base);

    const auto created = send(c,
                              "POST",
                              base,
                              "application/json",
                              json{{"event",
                                    {{"eventType", "NUM_OF_REGD_UES"},
                                     {"eventTrigger", "THRESHOLD"},
                                     {"eventFilter", json::array({{{"sst", 2}}})},
                                     {"notifThreshold", {{"numericValNumUes", 5}}}}},
                                   {"eventNotifyUri", "https://127.0.0.1:9/sac-report"},
                                   {"nfId", "00000000-0000-4000-8000-0000000000aa"},
                                   {"notifyCorrelationId", "corr-patch"}}
                                  .dump());
    ASSERT_EQ(created.status, 201) << created.body;
    const auto id = json::parse(created.body).value("subscriptionId", "");
    ASSERT_FALSE(id.empty()) << created.body;

    const auto r =
        send(c,
             "PATCH",
             base + "/" + id,
             kJsonPatch,
             R"([{"op":"replace","path":"/event/notifThreshold/numericValNumUes","value":9}])");
    ASSERT_EQ(r.status, 200) << r.body;
    const auto body = json::parse(r.body);
    EXPECT_EQ(body.value("subscriptionId", ""), id) << r.body;
    ASSERT_TRUE(body.contains("subscription")) << r.body;
    EXPECT_EQ(body["subscription"]["event"]["notifThreshold"].value("numericValNumUes", 0), 9)
        << r.body;

    EXPECT_EQ(send(c, "PATCH", base + "/" + id, kJsonPatch, R"({"event":{}})").status, 400);
}
