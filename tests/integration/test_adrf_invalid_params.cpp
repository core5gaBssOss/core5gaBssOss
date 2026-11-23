// SBI-03 invalidParams, step 2, ADRF (ADR-0624; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param encodings are TS29571_CommonData.yaml's:
// "query " plus the name for a query parameter, a JSON Pointer for a body attribute. Every name is
// the one in TS29575_Nadrf_DataManagement.yaml / TS29575_Nadrf_MLModelManagement.yaml (REL-19).
//
// One probe per ADRF site with a NOTE 1 cause; each answer comes from request validation, before
// any store is read. The ADRF itself only starts with Apache Doris, its model-store PostgreSQL
// (127.0.0.1:5438) and Valkey reachable (fail-fast, ADR-0367), as for test_adrf.cpp. No ADRF site
// with a NOTE 1 cause names a header or a path variable, so there is no probe for either.
// Real nrf + adrf processes over TLS 1.3 + mTLS HTTP/2 with real NRF tokens.
#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <utility>

#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

constexpr const char* kAdrfBase = "https://127.0.0.1:7804";
constexpr const char* kDm = "/nadrf-datamanagement/v1";
constexpr const char* kMl = "/nadrf-mlmodelmanagement/v1";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

nf_test::SpawnedProcess spawn_adrf_after_nrf() {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(ADRF_PATH);
}

class AdrfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess adrf_ = spawn_adrf_after_nrf();
    sbi_core::http2::Client client_ = make_client();

    void SetUp() override {
        ASSERT_GT(adrf_.pid(), 0);
        nf_test::attach_test_tokens(client_);
        // Ready once a tokened request reaches the handler: the bare RetrievalRequest is a 400.
        ASSERT_TRUE(nf_test::wait_until_ready(
            [this] {
                sbi_core::http2::ClientRequest req;
                req.method = "GET";
                req.url = std::string(kAdrfBase) + kDm + "/data-store-records";
                const auto r = client_.send(req);
                return r.has_value() && r->status == 400;
            },
            std::chrono::seconds(30)))
            << "adrf never answered (it needs Doris, PostgreSQL on 5438 and Valkey to start)";
    }

    json send(const char* method, const std::string& path, const std::string& body = "") {
        sbi_core::http2::ClientRequest req;
        req.method = method;
        req.url = std::string(kAdrfBase) + path;
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

json entry(const std::string& param, const std::string& reason) {
    return json{{"param", param}, {"reason", reason}};
}

} // namespace

TEST_F(AdrfInvalidParams, RetrievalRequestWithoutSelectorNamesAllThreeQueryParameters) {
    const auto problem = send("GET", std::string(kDm) + "/data-store-records");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const std::string reason = "absent; exactly one of store-trans-id, fetch-correlation-ids and "
                               "data-set-id is required";
    const json expected = json::array({entry("query store-trans-id", reason),
                                       entry("query fetch-correlation-ids", reason),
                                       entry("query data-set-id", reason)});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, RetrievalRequestWithTwoSelectorsNamesTheTwoGiven) {
    const auto problem =
        send("GET", std::string(kDm) + "/data-store-records?store-trans-id=a&data-set-id=b");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const std::string reason = "present with another; exactly one of store-trans-id, "
                               "fetch-correlation-ids and data-set-id is allowed";
    const json expected =
        json::array({entry("query store-trans-id", reason), entry("query data-set-id", reason)});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, StorageSubscriptionWithoutAnaSubOrDataSubNamesBoth) {
    const auto problem = send("POST", std::string(kDm) + "/request-storage-sub", "{}");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const std::string reason = "absent; one of anaSub and dataSub is required";
    const json expected = json::array({entry("/anaSub", reason), entry("/dataSub", reason)});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, StorageSubscriptionWithoutTargetNamesTargetNfIdAndTargetNfSetId) {
    const json body{
        {"anaSub", json{{"eventSubscriptions", json::array({json{{"event", "NF_LOAD"}}})}}}};
    const auto problem = send("POST", std::string(kDm) + "/request-storage-sub", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const std::string reason = "absent; one of targetNfId and targetNfSetId is required";
    const json expected =
        json::array({entry("/targetNfId", reason), entry("/targetNfSetId", reason)});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, StorageSubscriptionRemovalWithoutReferenceNamesBoth) {
    const auto problem = send("POST", std::string(kDm) + "/request-storage-sub-removal", "{}");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const std::string reason = "absent; one of transRefId and dataSetId is required";
    const json expected = json::array({entry("/transRefId", reason), entry("/dataSetId", reason)});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, RetrievalSubscriptionWithoutSelectorNamesAllThree) {
    const json body{
        {"notificationURI", "https://127.0.0.1:19993/notify"},
        {"notifCorrId", "adrf-invalid-params"},
        {"timePeriod",
         json{{"startTime", "2026-01-01T00:00:00Z"}, {"stopTime", "2026-01-02T00:00:00Z"}}}};
    const auto problem =
        send("POST", std::string(kDm) + "/data-retrieval-subscriptions", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const std::string reason = "absent; one of anaSub, dataSub and dataSetId is required";
    const json expected = json::array(
        {entry("/anaSub", reason), entry("/dataSub", reason), entry("/dataSetId", reason)});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, MlModelStoreRecordWithoutOwnerNamesNfInstanceIdAndNfSetId) {
    const json body{{"mlModels", json::array({json{{"modelUniqueId", 1}, {"mlModel", "AA=="}}})}};
    const auto problem = send("POST", std::string(kMl) + "/mlmodel-store-records", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const std::string reason = "absent; one of nfInstanceId and nfSetId is required";
    const json expected = json::array({entry("/nfInstanceId", reason), entry("/nfSetId", reason)});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, MlModelStoreRecordWithoutModelsNamesMlModelInfoAndMlModels) {
    const json body{{"nfInstanceId", "6c9e5f3a-1d2b-4c8e-9a10-000000000624"}};
    const auto problem = send("POST", std::string(kMl) + "/mlmodel-store-records", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const std::string reason = "absent; one of mlModelInfo and mlModels is required";
    const json expected = json::array({entry("/mlModelInfo", reason), entry("/mlModels", reason)});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, MlModelWithoutItsBytesNamesTheMemberByPointer) {
    const json body{{"nfInstanceId", "6c9e5f3a-1d2b-4c8e-9a10-000000000624"},
                    {"mlModels",
                     json::array({json{{"modelUniqueId", 1}, {"mlModel", "AA=="}},
                                  json{{"modelUniqueId", 2}}})}};
    const auto problem = send("POST", std::string(kMl) + "/mlmodel-store-records", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const json expected = json::array({entry("/mlModels/1/mlModel", "missing")});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, MlModelInfoWithoutFileAddressNamesTheMemberByPointer) {
    const json body{{"nfInstanceId", "6c9e5f3a-1d2b-4c8e-9a10-000000000624"},
                    {"mlModelInfo", json::array({json{{"modelUniqueId", 7}}})}};
    const auto problem = send("POST", std::string(kMl) + "/mlmodel-store-records", body.dump());
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const json expected = json::array({entry("/mlModelInfo/0/mlFileAddr", "missing")});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, MlModelRetrievalWithoutSelectorNamesBothQueryParameters) {
    const auto problem = send("GET", std::string(kMl) + "/mlmodel-store-records");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const std::string reason =
        "both absent; exactly one of store-trans-id and model-unique-ids is required";
    const json expected = json::array(
        {entry("query store-trans-id", reason), entry("query model-unique-ids", reason)});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, MlModelRetrievalWithNonIntegerIdNamesModelUniqueIds) {
    const auto problem =
        send("GET", std::string(kMl) + "/mlmodel-store-records?model-unique-ids=1,x");
    EXPECT_EQ(problem.value("cause", ""), "INVALID_QUERY_PARAM") << problem.dump();
    const json expected = json::array({entry("query model-unique-ids", "'x' is not an integer")});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}

TEST_F(AdrfInvalidParams, RemoveStoredMlModelWithEmptyListNamesTheWholeBody) {
    const auto problem = send("POST", std::string(kMl) + "/remove-stored-mlmodel", "[]");
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING") << problem.dump();
    const json expected = json::array({entry("", "empty array; minItems is 1")});
    EXPECT_EQ(problem.value("invalidParams", json()), expected) << problem.dump();
}
