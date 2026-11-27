// SBI-03 invalidParams, step 2, NWDAF (ADR-0625; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param encodings are TS29571_CommonData.yaml's:
// "query " plus the name for a query parameter, a JSON Pointer for a body attribute.
//
// One probe per reachable NWDAF NOTE 1 site, against real processes over TLS 1.3 + mTLS HTTP/2
// with real NRF tokens: an NRF, an NWDAF in role anlf (config/nwdaf.json, port 7798) and an NWDAF
// in role mtlf (port 7797, as test_nwdaf_ml_training_sub.cpp starts it). Both NWDAFs keep their
// state in Valkey; without one the test is skipped, not passed.
//
// Not probed, disclosed in ADR-0625: the NwdafMLModelTrainSubsc required-member and
// TrainingUnsubscribeInfo termCause sites (the generated DTO's from_json rejects those bodies
// first, in sbi-core, without invalidParams), and the MLModelMonitorReg modelId site (reached only
// by modelId 0, which the handler treats as absent).
#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <string>
#include <sw/redis++/redis++.h>
#include <thread>
#include <utility>

#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

constexpr const char* kAnlfBase = "https://127.0.0.1:7798";
constexpr const char* kMtlfBase = "https://127.0.0.1:7797";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

std::string redis_url() {
    const char* url = std::getenv("NWDAF_REDIS_URL");
    return url != nullptr ? url : "tcp://127.0.0.1:6379";
}

bool valkey_reachable() {
    try {
        sw::redis::Redis(redis_url()).ping();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

nf_test::SpawnedProcess spawn_anlf_after_nrf() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(NWDAF_PATH);
}

// The same binary in role mtlf on its own SBI and metrics ports (test_nwdaf_ml_training_sub.cpp).
nf_test::SpawnedProcess spawn_mtlf() {
    setenv("NWDAF_ROLE", "mtlf", 1);
    setenv("NWDAF_PORT", "7797", 1);
    setenv("NWDAF_METRICS_BIND_ADDRESS", "0.0.0.0:9486", 1);
    setenv("NWDAF_SELF_BASE_URL", kMtlfBase, 1);
    nf_test::SpawnedProcess mtlf(NWDAF_PATH);
    for (const char* k :
         {"NWDAF_ROLE", "NWDAF_PORT", "NWDAF_METRICS_BIND_ADDRESS", "NWDAF_SELF_BASE_URL"}) {
        unsetenv(k);
    }
    return mtlf;
}

class NwdafInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess anlf_ = spawn_anlf_after_nrf();
    nf_test::SpawnedProcess mtlf_ = spawn_mtlf();
    sbi_core::http2::Client client_ = make_client();

    void SetUp() override {
        if (!valkey_reachable()) {
            GTEST_SKIP() << "no Valkey at " << redis_url() << " -- skipped, not passed";
        }
        ASSERT_GT(anlf_.pid(), 0);
        ASSERT_GT(mtlf_.pid(), 0);
        nf_test::attach_test_tokens(client_);
        for (const std::string& url :
             {std::string(kAnlfBase) + "/nnwdaf-analyticsinfo/v1/context",
              std::string(kMtlfBase) + "/nnwdaf-mlmodeltraining/v1/subscriptions"}) {
            ASSERT_TRUE(nf_test::wait_until_ready(
                [this, &url] {
                    sbi_core::http2::ClientRequest req;
                    req.method = "GET";
                    req.url = url;
                    const auto r = client_.send(req);
                    return r.has_value() && r->status != 0;
                },
                std::chrono::seconds(30)))
                << url << " never reachable";
        }
    }

    // Sends the request and returns the ProblemDetails of the 400 it expects.
    json send(const char* method,
              const std::string& base,
              const std::string& path,
              const std::string& body) {
        sbi_core::http2::ClientRequest req;
        req.method = method;
        req.url = base + path;
        if (!body.empty()) {
            req.headers.emplace("content-type", "application/json");
            req.body = body;
        }
        auto resp = client_.send(req);
        if (!resp.has_value()) {
            ADD_FAILURE() << method << " " << path << ": no response";
            return json::object();
        }
        EXPECT_EQ(resp->status, 400) << method << " " << path << " " << resp->body;
        return json::parse(resp->body, nullptr, false);
    }
};

json one_of_missing(const char* a, const char* b, const std::string& reason) {
    return json::array({{{"param", std::string("/") + a}, {"reason", reason}},
                        {{"param", std::string("/") + b}, {"reason", reason}}});
}

} // namespace

// AnLF, Nnwdaf_AnalyticsInfo GetNWDAFAnalytics: event-id is the YAML's only required query
// parameter (TS29520_Nnwdaf_AnalyticsInfo.yaml).
TEST_F(NwdafInvalidParams, AnalyticsWithoutEventIdNamesTheQueryParameter) {
    const auto problem = send("GET", kAnlfBase, "/nnwdaf-analyticsinfo/v1/analytics", "");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_QUERY_PARAM_MISSING") << problem.dump();
    const json expected = json::array({{{"param", "query event-id"}, {"reason", "missing"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

// AnLF, Nnwdaf_MLModelMonitor_Subscribe: MLModelMonitorSub.modelIds has minItems 1.
TEST_F(NwdafInvalidParams, MonitorSubscriptionWithEmptyModelIdsNamesModelIds) {
    const json body{{"modelIds", json::array()},
                    {"notificationUri", "https://127.0.0.1:19995/monitor"},
                    {"notifCorrId", "corr-invalid-params"}};
    const auto problem =
        send("POST", kAnlfBase, "/nnwdaf-mlmodelmonitor/v1/subscriptions", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const json expected = json::array({{{"param", "/modelIds"}, {"reason", "empty; minItems 1"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

// AnLF, the Nnwdaf_DataManagement Fetch callback: the body is the array itself, so the pointer is
// "" (RFC 6901: the whole document).
TEST_F(NwdafInvalidParams, FetchWithNoCorrelationIdsNamesTheWholeBody) {
    const auto problem = send("POST", kAnlfBase, "/nwdaf-inbound/v1/fetch", "[]");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const json expected = json::array({{{"param", ""}, {"reason", "empty; minItems 1"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

// AnLF, Nnwdaf_DataManagement_Subscribe: NnwdafDataManagementSubsc oneOf anaSub / dataSub.
TEST_F(NwdafInvalidParams, DataManagementWithoutAnaSubOrDataSubNamesBoth) {
    const json body{{"notifCorrId", "corr-invalid-params"},
                    {"notificURI", "https://127.0.0.1:19995/dm"}};
    const auto problem =
        send("POST", kAnlfBase, "/nnwdaf-datamanagement/v1/subscriptions", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    EXPECT_EQ(problem.value("invalidParams", json()),
              one_of_missing("anaSub", "dataSub", "missing; one of anaSub, dataSub is required"))
        << problem.dump();
}

// MTLF, Nnwdaf_MLModelProvision_Subscribe: the second MLEventSubscription has no mLEvent (and no
// posModelReqInd), so the pointer names index 1.
TEST_F(NwdafInvalidParams, ProvisionSubscriptionWithoutMlEventNamesTheItem) {
    const json body{{"mLEventSubscs",
                     json::array({json{{"mLEvent", "NF_LOAD"}, {"mLEventFilter", json::object()}},
                                  json{{"mLEventFilter", json::object()}}})},
                    {"notifUri", "https://127.0.0.1:19995/provision"}};
    const auto problem =
        send("POST", kMtlfBase, "/nnwdaf-mlmodelprovision/v1/subscriptions", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const json expected =
        json::array({{{"param", "/mLEventSubscs/1/mLEvent"}, {"reason", "missing"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

// MTLF, Nnwdaf_MLModelMonitor_Register: MLModelMonitorReg oneOf consumerId / consumerSetId.
TEST_F(NwdafInvalidParams, MonitorRegistrationWithoutConsumerNamesBoth) {
    const json body{{"modelId", 1}};
    const auto problem =
        send("POST", kMtlfBase, "/nnwdaf-mlmodelmonitor/v1/registrations", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    EXPECT_EQ(problem.value("invalidParams", json()),
              one_of_missing("consumerId",
                             "consumerSetId",
                             "missing; one of consumerId, consumerSetId is required"))
        << problem.dump();
}

// MTLF, Nnwdaf_MLModelMonitor_Notify inbound: MLModelMonitorNotify anyOf modelAccuInfos /
// anaFeedbacks.
TEST_F(NwdafInvalidParams, MonitorNotifyWithoutAccuracyOrFeedbackNamesBoth) {
    const json body{{"notifCorrId", "corr-invalid-params"}};
    const auto problem = send("POST", kMtlfBase, "/nwdaf-inbound/v1/ml-monitor", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    EXPECT_EQ(problem.value("invalidParams", json()),
              one_of_missing("modelAccuInfos",
                             "anaFeedbacks",
                             "missing; at least one of modelAccuInfos, anaFeedbacks is required"))
        << problem.dump();
}

// MTLF, Nnwdaf_MLModelTraining_Subscribe: an MLEventSubscription without mLEvent.
TEST_F(NwdafInvalidParams, TrainingSubscriptionWithoutMlEventNamesTheItem) {
    const json body{{"mLEventSubscs", json::array({json{{"mLEventFilter", json::object()}}})},
                    {"notifUri", "https://127.0.0.1:19995/training"},
                    {"notifCorreId", "corr-invalid-params"}};
    const auto problem =
        send("POST", kMtlfBase, "/nnwdaf-mlmodeltraining/v1/subscriptions", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const json expected =
        json::array({{{"param", "/mLEventSubscs/0/mLEvent"}, {"reason", "missing"}}});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}
