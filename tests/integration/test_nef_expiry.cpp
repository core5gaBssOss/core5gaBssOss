// ADR-0538 (REVIEW-5ae0ef1 SUS-02): the NEF sweeps the expiries its APIs make mandatory and caps
// the expiries a consumer proposes. Spawns NRF + NEF (same scaffold as test_nef_store_bounds.cpp).

#include "sbi_core/datetime.hpp"
#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "location_path.hpp"
#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

constexpr const char* kNefBase = "https://127.0.0.1:7790";

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

struct Duo {
    nf_test::SpawnedProcess nrf;
    nf_test::SpawnedProcess nef;
};

// Spawns nrf + nef with `env` (name, value) applied to the nef only.
Duo spawn(std::initializer_list<std::pair<const char*, const char*>> env) {
    nf_test::SpawnedProcess nrf(NRF_PATH);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    for (const auto& [name, value] : env) {
        setenv(name, value, 1);
    }
    nf_test::SpawnedProcess nef(NEF_PATH);
    for (const auto& [name, value] : env) {
        unsetenv(name);
    }
    return Duo{std::move(nrf), std::move(nef)};
}

tl::expected<sbi_core::http2::ClientResponse, std::string>
post(sbi_core::http2::Client& client, const std::string& url, const json& body) {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = url;
    req.headers.emplace("content-type", "application/json");
    req.body = body.dump();
    return client.send(req);
}

json time_sync_subscription() {
    return json{{"anyUeInd", true},
                {"subsNotifUri", "https://af.example.com/time-sync-notify"},
                {"subsNotifId", "notif-sus02"},
                {"subscribedEvents", json::array({"AVAILABILITY_FOR_TIME_SYNC_SERVICE"})}};
}

// TS29591 NefEventExposureSubsc: eventsSubs, notifId and notifUri are required.
json event_exposure(const std::optional<std::string>& mon_dur) {
    json j{{"eventsSubs", json::array({{{"event", "UE_MOBILITY"}}})},
           {"notifId", "notif-expiry"},
           {"notifUri", "https://af.example.com/ee-notify"}};
    if (mon_dur) {
        j["eventsRepInfo"] = {{"monDur", *mon_dur}};
    }
    return j;
}

std::string from_now(std::chrono::seconds offset) {
    return sbi_core::format_rfc3339(std::chrono::system_clock::now() + offset);
}

// POST `body` to `collection`; the absolute URL of the created resource ("" on failure).
std::string
create(sbi_core::http2::Client& client, const std::string& collection, const json& body) {
    const auto r = post(client, collection, body);
    if (!r.has_value() || r->status != 201) {
        ADD_FAILURE() << collection << ": "
                      << (r ? std::to_string(r->status) + " " + r->body : r.error());
        return "";
    }
    const auto location = r->headers.find("location");
    if (location == r->headers.end()) {
        ADD_FAILURE() << collection << ": 201 without Location";
        return "";
    }
    return std::string(kNefBase) + nf_test::location_path(location->second);
}

tl::expected<sbi_core::http2::ClientResponse, std::string> get(sbi_core::http2::Client& client,
                                                               const std::string& url) {
    sbi_core::http2::ClientRequest req;
    req.method = "GET";
    req.url = url;
    return client.send(req);
}

int status_of(sbi_core::http2::Client& client, const std::string& url) {
    const auto r = get(client, url);
    return r.has_value() ? r->status : 0;
}

} // namespace

// ADR-0538 (REVIEW-5ae0ef1 SUS-02): what has expired is gone after a sweep, what has not stays,
// and a CP provisioning keeps its unexpired sets. Sweep every second.
TEST(NefExpiry, ExpiredResourcesAreSweptAndCurrentOnesStay) {
    auto d = spawn({{"NEF_STORE_SWEEP_INTERVAL_SECONDS", "1"}});
    ASSERT_GT(d.nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string np = std::string(kNefBase) +
                           "/3gpp-network-parameter-configuration/v1/scs-expiry/configurations";
    ASSERT_TRUE(wait_reachable(client, np, 200)) << "nef never reachable";

    const auto soon = from_now(std::chrono::seconds(2));
    const auto later = from_now(std::chrono::hours(1));
    const auto np_soon =
        create(client, np, json{{"externalId", "ue1@example.com"}, {"validityTime", soon}});
    const auto np_later =
        create(client, np, json{{"externalId", "ue2@example.com"}, {"validityTime", later}});
    const auto ee = create(client,
                           std::string(kNefBase) + "/nnef-eventexposure/v1/subscriptions",
                           event_exposure(soon));
    json ts = time_sync_subscription();
    ts["expiry"] = soon;
    const auto time_sync =
        create(client, std::string(kNefBase) + "/3gpp-time-sync/v1/af-expiry/subscriptions", ts);
    const auto cp = create(client,
                           std::string(kNefBase) +
                               "/3gpp-cp-parameter-provisioning/v1/scs-expiry/subscriptions",
                           json{{"cpParameterSets",
                                 json::array({{{"setId", "short"}, {"validityTime", soon}},
                                              {{"setId", "long"}, {"validityTime", later}}})}});
    for (const auto* url : {&np_soon, &np_later, &ee, &time_sync, &cp}) {
        ASSERT_FALSE(url->empty());
        EXPECT_EQ(status_of(client, *url), 200) << *url;
    }

    std::this_thread::sleep_for(std::chrono::seconds(4));
    EXPECT_EQ(status_of(client, np_soon), 404) << "validityTime passed (TS 29.122 5.13.2.1.2)";
    EXPECT_EQ(status_of(client, np_later), 200);
    EXPECT_EQ(status_of(client, ee), 404) << "monDur passed (TS 29.591 4.2.2.2.2)";
    EXPECT_EQ(status_of(client, time_sync), 404) << "expiry passed (TS 29.522 5.15.4.3.2)";
    const auto kept = get(client, cp);
    ASSERT_TRUE(kept.has_value());
    ASSERT_EQ(kept->status, 200) << kept->body;
    const auto sets = json::parse(kept->body).at("cpParameterSets");
    ASSERT_EQ(sets.size(), 1U) << kept->body;
    EXPECT_EQ(sets[0].at("setId"), "long") << "CpParameterSet validityTime (TS 29.122 5.10.2.2.2)";
}

// ADR-0538: a proposed expiry beyond subscription_max_lifetime_seconds is confirmed no later than
// that; an absent one is not added.
TEST(NefExpiry, AProposedExpiryBeyondTheMaximumLifetimeIsCapped) {
    auto d = spawn({{"NEF_SUBSCRIPTION_MAX_LIFETIME_SECONDS", "60"}});
    ASSERT_GT(d.nef.pid(), 0);
    auto client = make_client();
    nf_test::attach_test_tokens(client);
    const std::string collection = std::string(kNefBase) + "/nnef-eventexposure/v1/subscriptions";
    ASSERT_TRUE(wait_reachable(client, collection, 200)) << "nef never reachable";

    const auto before = std::chrono::system_clock::now();
    const auto capped = post(client, collection, event_exposure("2099-01-01T00:00:00Z"));
    ASSERT_TRUE(capped.has_value());
    ASSERT_EQ(capped->status, 201) << capped->body;
    const auto mon_dur = sbi_core::parse_rfc3339(
        json::parse(capped->body).at("eventsRepInfo").at("monDur").get<std::string>());
    ASSERT_TRUE(mon_dur.has_value()) << capped->body;
    EXPECT_LE(*mon_dur, std::chrono::system_clock::now() + std::chrono::seconds(60))
        << capped->body;
    EXPECT_GT(*mon_dur, before) << capped->body;

    const auto open = post(client, collection, event_exposure(std::nullopt));
    ASSERT_TRUE(open.has_value());
    ASSERT_EQ(open->status, 201) << open->body;
    EXPECT_FALSE(json::parse(open->body).contains("eventsRepInfo")) << open->body;
}
