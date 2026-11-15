// Drives nrf as a real, separate OS process to exercise its own Tier-B gap-closure operations
// (ADR-0211, docs/CAPABILITY_GAP_ANALYSIS.md's own NRF audit): OptionsNFInstances
// (TS29510_Nnrf_NFManagement.yaml), RetrieveStoredSearch/RetrieveCompleteSearch, and
// RetrieveKeyRequest (TS29510_Nnrf_AccessToken.yaml) -- over real TLS 1.3 + mTLS HTTP/2 with a
// real signed OAuth2 token.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <csignal>
#include <fstream>
#include <sstream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>

#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

// NRF's own real, fixed nfInstanceId (nfs/nrf/src/main.cpp's own kNrfInstanceId) -- see
// docs/DECISIONS.md ADR-0018 for why it's fixed rather than randomly generated per run.
constexpr const char* kNrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

bool wait_reachable(sbi_core::http2::Client& client, const std::string& url, int max_attempts) {
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = url;
        if (client.send(req).has_value()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

std::string fetch_token(sbi_core::http2::Client& client, const std::string& scope) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body =
        "grant_type=client_credentials&nfInstanceId=" + std::string(nf_test::kTestAfInstanceId) +
        "&scope=" + scope + "&targetNfType=NRF";
    auto resp = nf_test::send_token_request(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

// Same real PEM-body extraction the server itself does -- used here only to compute the expected
// value independently, not to re-test the server against its own implementation.
std::string expected_raw_pub_key() {
    std::ifstream key_file(std::string(CERTS_DIR) + "/nrf-jwt/public.pem");
    std::string raw;
    std::string line;
    while (std::getline(key_file, line)) {
        if (line.find("-----") == std::string::npos) {
            raw += line;
        }
    }
    return raw;
}

struct Solo {
    nf_test::SpawnedProcess nrf;
};

Solo spawn_nrf() {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    return Solo{std::move(nrf)};
}

} // namespace

TEST(NrfGapClosureIntegration, OptionsNFInstancesReturnsRealAcceptEncoding) {
    auto s = spawn_nrf();
    auto client = make_client();
    ASSERT_TRUE(
        wait_reachable(client, "https://127.0.0.1:7777/nnrf-nfm/v1/nf-instances/nonexistent", 200))
        << "nrf never became reachable";

    const std::string token = fetch_token(client, "nnrf-nfm");
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    sbi_core::http2::ClientRequest req;
    req.method = "OPTIONS";
    req.url = "https://127.0.0.1:7777/nnrf-nfm/v1/nf-instances";
    req.headers.emplace("authorization", "Bearer " + token);
    auto resp = client.send(req);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->status, 200);
    auto it = resp->headers.find("accept-encoding");
    EXPECT_NE(it, resp->headers.end());
}

TEST(NrfGapClosureIntegration, RetrieveKeyRequestForOwnIssuerReturnsRealPublicKey) {
    auto s = spawn_nrf();
    auto client = make_client();
    ASSERT_TRUE(
        wait_reachable(client, "https://127.0.0.1:7777/nnrf-nfm/v1/nf-instances/nonexistent", 200))
        << "nrf never became reachable";

    const std::string token = fetch_token(client, "nnrf-nfm");
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    const json req_body = json{
        {"issuer", kNrfInstanceId},
        {"headerParameters", json{{"kid", "1"}}},
    };
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/retrieve-key";
    req.headers.emplace("content-type", "application/json");
    req.headers.emplace("authorization", "Bearer " + token);
    req.body = req_body.dump();
    auto resp = client.send(req);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->status, 200) << resp->body;
    if (resp->status == 200) {
        const auto rsp = json::parse(resp->body);
        ASSERT_TRUE(rsp.contains("rawPubKey"));
        // Real, live-verified: matches the actual certs/nrf-jwt/public.pem this NRF process is
        // running with, not a placeholder value.
        EXPECT_EQ(rsp.at("rawPubKey").get<std::string>(), expected_raw_pub_key());
    }
}

TEST(NrfGapClosureIntegration, RetrieveKeyRequestForUnknownIssuerIs404) {
    auto s = spawn_nrf();
    auto client = make_client();
    ASSERT_TRUE(
        wait_reachable(client, "https://127.0.0.1:7777/nnrf-nfm/v1/nf-instances/nonexistent", 200))
        << "nrf never became reachable";

    const std::string token = fetch_token(client, "nnrf-nfm");
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    const json req_body = json{
        {"issuer", "00000000-0000-4000-8000-000000000099"},
        {"headerParameters", json{{"kid", "1"}}},
    };
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/retrieve-key";
    req.headers.emplace("content-type", "application/json");
    req.headers.emplace("authorization", "Bearer " + token);
    req.body = req_body.dump();
    auto resp = client.send(req);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->status, 404);
}

TEST(NrfGapClosureIntegration, RetrieveStoredSearchAndCompleteSearchReturnCachedResult) {
    auto s = spawn_nrf();
    auto client = make_client();
    ASSERT_TRUE(
        wait_reachable(client, "https://127.0.0.1:7777/nnrf-nfm/v1/nf-instances/nonexistent", 200))
        << "nrf never became reachable";

    const std::string token = fetch_token(client, "nnrf-nfm nnrf-disc");
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    // Register one real NF instance so the discovery search below returns a non-empty result.
    const std::string nf_id = "11111111-2222-4333-8444-555555555555";
    const json profile = json{
        {"nfInstanceId", nf_id},
        {"nfType", "UDM"},
        {"nfStatus", "REGISTERED"},
    };
    sbi_core::http2::ClientRequest put_req;
    put_req.method = "PUT";
    put_req.url = "https://127.0.0.1:7777/nnrf-nfm/v1/nf-instances/" + nf_id;
    put_req.headers.emplace("content-type", "application/json");
    put_req.headers.emplace("authorization", "Bearer " + token);
    put_req.body = profile.dump();
    auto put_resp = client.send(put_req);
    ASSERT_TRUE(put_resp.has_value());
    EXPECT_EQ(put_resp->status, 201) << put_resp->body;

    sbi_core::http2::ClientRequest search_req;
    search_req.method = "GET";
    search_req.url = "https://127.0.0.1:7777/nnrf-disc/v1/nf-instances?target-nf-type=UDM";
    search_req.headers.emplace("authorization", "Bearer " + token);
    auto search_resp = client.send(search_req);
    ASSERT_TRUE(search_resp.has_value());
    EXPECT_EQ(search_resp->status, 200) << search_resp->body;

    std::string search_id;
    if (search_resp->status == 200) {
        const auto search_json = json::parse(search_resp->body);
        ASSERT_TRUE(search_json.contains("searchId"));
        search_id = search_json.at("searchId").get<std::string>();
        ASSERT_TRUE(search_json.contains("nfInstances"));
        EXPECT_GE(search_json.at("nfInstances").size(), 1u);
    }
    ASSERT_FALSE(search_id.empty());

    sbi_core::http2::ClientRequest stored_req;
    stored_req.method = "GET";
    stored_req.url = "https://127.0.0.1:7777/nnrf-disc/v1/searches/" + search_id;
    stored_req.headers.emplace("authorization", "Bearer " + token);
    auto stored_resp = client.send(stored_req);
    ASSERT_TRUE(stored_resp.has_value());
    EXPECT_EQ(stored_resp->status, 200) << stored_resp->body;
    if (stored_resp->status == 200) {
        const auto stored_json = json::parse(stored_resp->body);
        ASSERT_TRUE(stored_json.contains("nfInstances"));
        EXPECT_GE(stored_json.at("nfInstances").size(), 1u);
    }

    sbi_core::http2::ClientRequest complete_req;
    complete_req.method = "GET";
    complete_req.url = "https://127.0.0.1:7777/nnrf-disc/v1/searches/" + search_id + "/complete";
    complete_req.headers.emplace("authorization", "Bearer " + token);
    auto complete_resp = client.send(complete_req);
    ASSERT_TRUE(complete_resp.has_value());
    EXPECT_EQ(complete_resp->status, 200) << complete_resp->body;

    sbi_core::http2::ClientRequest bad_req;
    bad_req.method = "GET";
    bad_req.url = "https://127.0.0.1:7777/nnrf-disc/v1/searches/nonexistent-search-id";
    bad_req.headers.emplace("authorization", "Bearer " + token);
    auto bad_resp = client.send(bad_req);
    ASSERT_TRUE(bad_resp.has_value());
    EXPECT_EQ(bad_resp->status, 404);
}

// ADR-0574: NFUpdate (TS 29.510 5.2.2.3.1B, 5.2.2.3.2), NFDeregister (5.2.2.4.2),
// UpdateSubscription (5.2.2.5.6) and NFStatusUnSubscribe (5.2.2.7.2) against the real nrf binary.
namespace {

constexpr const char* kNfm = "https://127.0.0.1:7777/nnrf-nfm/v1";
constexpr const char* kUpdId = "4947a69a-f61b-4bc1-b9da-47c9c5d14b64";

sbi_core::http2::ClientRequest
nfm_request(const std::string& method, const std::string& path, const std::string& token) {
    sbi_core::http2::ClientRequest req;
    req.method = method;
    req.url = std::string(kNfm) + path;
    if (!token.empty()) {
        req.headers.emplace("authorization", "Bearer " + token);
    }
    return req;
}

sbi_core::http2::ClientRequest
json_patch(const std::string& path, const std::string& token, const json& ops) {
    auto req = nfm_request("PATCH", path, token);
    req.headers.emplace("content-type", "application/json-patch+json");
    req.body = ops.dump();
    return req;
}

} // namespace

TEST(NrfNfUpdateIntegration, PatchHeartBeatDeregisterAndSubscriptionUpdate) {
    auto s = spawn_nrf();
    auto client = make_client();
    ASSERT_TRUE(
        wait_reachable(client, "https://127.0.0.1:7777/nnrf-nfm/v1/nf-instances/nonexistent", 200))
        << "nrf never became reachable";
    const std::string token = fetch_token(client, "nnrf-nfm");
    ASSERT_FALSE(token.empty()) << "failed to obtain OAuth2 token from nrf";

    const std::string item = std::string("/nf-instances/") + kUpdId;
    auto put = nfm_request("PUT", item, "");
    put.headers.emplace("content-type", "application/json");
    put.body = json{{"nfInstanceId", kUpdId}, {"nfType", "AMF"}, {"nfStatus", "REGISTERED"}}.dump();
    auto r = client.send(put);
    ASSERT_TRUE(r.has_value()) << r.error();
    ASSERT_EQ(r->status, 201) << r->body;
    ASSERT_EQ(r->headers.count("etag"), 1u);
    const std::string put_etag = r->headers.find("etag")->second;

    // GET carries the same strong entity tag.
    r = client.send(nfm_request("GET", item, token));
    ASSERT_TRUE(r.has_value()) << r.error();
    ASSERT_EQ(r->status, 200) << r->body;
    EXPECT_EQ(r->headers.find("etag")->second, put_etag);

    // Heart-beat, the 5.2.2.3.2 EXAMPLE shape without If-Match: 204, no new entity tag.
    const json heartbeat =
        json::array({{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}});
    r = client.send(json_patch(item, token, heartbeat));
    ASSERT_TRUE(r.has_value()) << r.error();
    EXPECT_EQ(r->status, 204) << r->body;
    r = client.send(nfm_request("GET", item, token));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->headers.find("etag")->second, put_etag);

    // A merge-patch body: 415 with Accept-Patch.
    auto merge = nfm_request("PATCH", item, token);
    merge.headers.emplace("content-type", "application/merge-patch+json");
    merge.body = R"({"nfStatus":"REGISTERED"})";
    r = client.send(merge);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 415) << r->body;
    EXPECT_EQ(r->headers.count("accept-patch"), 1u);

    // A stale If-Match: 412, profile untouched.
    auto conditional = json_patch(
        item,
        token,
        json::array({{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "UNDISCOVERABLE"}}}));
    conditional.headers.emplace("if-match", "\"stale\"");
    r = client.send(conditional);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 412) << r->body;

    // Replacing a non-existing IE: 409. Removing nfType: 403 MODIFICATION_NOT_ALLOWED.
    r = client.send(json_patch(
        item, token, json::array({{{"op", "replace"}, {"path", "/priority"}, {"value", 1}}})));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 409) << r->body;
    r = client.send(
        json_patch(item, token, json::array({{{"op", "remove"}, {"path", "/nfType"}}})));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 403) << r->body;
    EXPECT_EQ(json::parse(r->body).value("cause", ""), "MODIFICATION_NOT_ALLOWED");

    // A conditional update with the current tag: 204, and the tag moves.
    auto current = json_patch(
        item,
        token,
        json::array({{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "UNDISCOVERABLE"}}}));
    current.headers.emplace("if-match", put_etag);
    r = client.send(current);
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 204) << r->body;
    r = client.send(nfm_request("GET", item, token));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(json::parse(r->body).at("nfStatus"), "UNDISCOVERABLE");
    EXPECT_NE(r->headers.find("etag")->second, put_etag);

    // NFDeregister: 204, then 404 for the same id; PATCH of it is 404 too.
    r = client.send(nfm_request("DELETE", item, token));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 204) << r->body;
    r = client.send(nfm_request("DELETE", item, token));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 404) << r->body;
    r = client.send(json_patch(item, token, heartbeat));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 404) << r->body;

    // UpdateSubscription: a JSON Patch replacing validityTime -> 204 (the value is accepted as
    // suggested); another attribute -> 400; unknown id -> 404 SUBSCRIPTION_NOT_FOUND.
    auto sub = nfm_request("POST", "/subscriptions", token);
    sub.headers.emplace("content-type", "application/json");
    sub.body = json{{"nfStatusNotificationUri", "https://127.0.0.1:1/notify"}}.dump();
    r = client.send(sub);
    ASSERT_TRUE(r.has_value()) << r.error();
    ASSERT_EQ(r->status, 201) << r->body;
    const std::string sub_item =
        "/subscriptions/" + json::parse(r->body).at("subscriptionId").get<std::string>();
    r = client.send(json_patch(
        sub_item,
        token,
        json::array(
            {{{"op", "replace"}, {"path", "/validityTime"}, {"value", "2030-12-30T23:20:50Z"}}})));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 204) << r->body;
    r = client.send(json_patch(
        sub_item,
        token,
        json::array({{{"op", "replace"}, {"path", "/nfStatusNotificationUri"}, {"value", "x"}}})));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 400) << r->body;
    r = client.send(json_patch(
        "/subscriptions/no-such-id",
        token,
        json::array(
            {{{"op", "replace"}, {"path", "/validityTime"}, {"value", "2030-12-30T23:20:50Z"}}})));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 404) << r->body;
    EXPECT_EQ(json::parse(r->body).value("cause", ""), "SUBSCRIPTION_NOT_FOUND");

    // NFStatusUnSubscribe: 204, then 404 SUBSCRIPTION_NOT_FOUND.
    r = client.send(nfm_request("DELETE", sub_item, token));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 204) << r->body;
    r = client.send(nfm_request("DELETE", sub_item, token));
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->status, 404) << r->body;
    EXPECT_EQ(json::parse(r->body).value("cause", ""), "SUBSCRIPTION_NOT_FOUND");
}
