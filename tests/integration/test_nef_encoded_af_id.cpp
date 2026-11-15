// ADR-0575 (ADR-0570 class, NEF part): the NEF puts the {afId} of its own request path into the
// UDR resource id `{afId}-{subscriptionId}` (TS29504_Nudr_DR serviceParamData/{serviceParamId},
// iptvConfigData/{configurationId}). The sbi_core server hands the handler the percent-decoded
// value (ADR-0549), so an AF that sends `victim%3F` as its afId used to make the NEF PUT
// `.../serviceParamData/victim?-<sub>` -- and the UDR stored the document under `victim`, a
// resource that belongs to someone else. With the fix the whole id is re-encoded as one segment.
//
// Drives nrf, udr and nef as real processes over TLS 1.3 + mTLS HTTP/2 with real NRF tokens.
// Needs the UDR's Postgres (udr-postgres-local locally, a service container in CI).

#include "sbi_core/http2_client.hpp"
#include "sbi_core/uri.hpp"

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

constexpr const char* kNefBase = "https://127.0.0.1:7790";
constexpr const char* kUdrServiceParamData =
    "https://127.0.0.1:7781/nudr-dr/v2/application-data/serviceParamData/";
// Decoded by the NEF's server to "victim-adr0575?", a value with a `?` in it.
constexpr const char* kInjectedAfIdOnTheWire = "victim-adr0575%3F";
constexpr const char* kVictimId = "victim-adr0575";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

bool wait_reachable(sbi_core::http2::Client& client, const std::string& url, int attempts) {
    for (int i = 0; i < attempts; ++i) {
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

struct Trio {
    nf_test::SpawnedProcess nrf;
    nf_test::SpawnedProcess udr;
    nf_test::SpawnedProcess nef;
};

Trio spawn_all() {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    nf_test::SpawnedProcess udr(UDR_PATH);
    nf_test::wait_tcp_listening(7781);
    nf_test::SpawnedProcess nef(NEF_PATH);
    return Trio{std::move(nrf), std::move(udr), std::move(nef)};
}

int udr_delete(sbi_core::http2::Client& client, const std::string& service_param_id) {
    sbi_core::http2::ClientRequest req;
    req.method = "DELETE";
    req.url = kUdrServiceParamData + sbi_core::uri::encode_path_segment(service_param_id);
    auto resp = client.send(req);
    return resp.has_value() ? resp->status : 0;
}

} // namespace

TEST(NefEncodedAfId, ServiceParameterAfIdWithQueryCharacterNeverLandsOnAnotherUdrResource) {
    auto d = spawn_all();
    ASSERT_GT(d.nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string collection = std::string(kNefBase) + "/3gpp-service-parameter/v1/" +
                                   kInjectedAfIdOnTheWire + "/subscriptions";
    ASSERT_TRUE(wait_reachable(client, collection, 200)) << "nef never reachable";
    // A leftover from an earlier run would make the check below meaningless.
    udr_delete(client, kVictimId);

    sbi_core::http2::ClientRequest post;
    post.method = "POST";
    post.url = collection;
    post.headers.emplace("content-type", "application/json");
    post.body = json{{"afServiceId", "svc-adr0575"}, {"anyUeInd", true}}.dump();
    auto created = client.send(post);
    ASSERT_TRUE(created.has_value()) << created.error();
    ASSERT_EQ(created->status, 201) << created->body;
    const auto location = created->headers.find("location");
    ASSERT_NE(location, created->headers.end());
    const std::string sub_id = location->second.substr(location->second.find_last_of('/') + 1);
    ASSERT_FALSE(sub_id.empty());

    // The document is at the one resource `{afId}-{subscriptionId}`, the afId with its `?` ...
    EXPECT_EQ(udr_delete(client, std::string(kVictimId) + "?-" + sub_id), 204);
    // ... and nothing was written to the resource the `?` would have cut the path at.
    EXPECT_EQ(udr_delete(client, kVictimId), 404);
}
