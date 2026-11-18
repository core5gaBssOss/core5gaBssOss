// ADR-0569: TMF640 POST /service validation (bss/provisioning/src/tmf640_service.cpp) against the
// rows of docs/PROVISIONING_MAPPING.md Part A that the user approved on 2026-11-07 (ADR-0558).
// Pure checks: no database, no network.

#include <nlohmann/json.hpp>

#include <string>

#include "tmf640_events.hpp"
#include "tmf640_service.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;
using provisioning::tmf640::validate_create;

bool known_profile(const std::string& id) {
    return id == "rfs-5g-data";
}

json characteristic(const std::string& name, const json& value) {
    return json{{"name", name}, {"valueType", "string"}, {"value", value}};
}

json valid_body() {
    return json{{"@type", "ResourceFacingService"},
                {"category", "RFS"},
                {"state", "active"},
                {"startDate", "2026-11-08T00:00:00Z"},
                {"serviceSpecification",
                 {{"id", "rfs-5g-data"}, {"@referredType", "ServiceSpecification"}}},
                {"serviceCharacteristic",
                 json::array({characteristic("supi", "imsi-999700000012345"),
                              characteristic("msisdn", "9997000123")})}};
}

std::string refusal(const json& body) {
    const auto v = validate_create(body.dump(), known_profile);
    EXPECT_FALSE(v.has_value()) << body.dump();
    if (v.has_value()) {
        return "";
    }
    EXPECT_EQ(v.error().status, 400);
    return v.error().detail;
}

} // namespace

TEST(Tmf640Service, AcceptsAnActiveRfsAndKeepsEveryTmfField) {
    const auto v = validate_create(valid_body().dump(), known_profile);
    ASSERT_TRUE(v.has_value()) << v.error().detail;
    EXPECT_EQ(v->supi, "imsi-999700000012345");
    EXPECT_EQ(v->msisdn, "9997000123");
    EXPECT_EQ(v->specification_id, "rfs-5g-data");
    EXPECT_EQ(v->state, "active");
    // A6: startDate is kept, not acted on; @type and @referredType survive the DTO.
    EXPECT_EQ(v->service["startDate"], "2026-11-08T00:00:00Z");
    EXPECT_EQ(v->service["@type"], "ResourceFacingService");
    EXPECT_EQ(v->service["serviceSpecification"]["@referredType"], "ServiceSpecification");
    EXPECT_FALSE(v->service.contains("id")); // assigned by the store
}

TEST(Tmf640Service, AcceptsEveryPreActiveStateAndAnAbsentMsisdn) {
    for (const char* state : {"feasibilityChecked", "designed", "reserved", "inactive"}) {
        json body = valid_body();
        body["state"] = state;
        body["serviceCharacteristic"] =
            json::array({characteristic("supi", "imsi-999700000012345")});
        const auto v = validate_create(body.dump(), known_profile);
        ASSERT_TRUE(v.has_value()) << state << ": " << v.error().detail;
        EXPECT_EQ(v->state, state);
        EXPECT_TRUE(v->msisdn.empty());
    }
}

TEST(Tmf640Service, RefusesTerminatedAndStatesOutsideServiceStateType) {
    json body = valid_body();
    body["state"] = "terminated"; // A5
    EXPECT_NE(refusal(body).find("A5"), std::string::npos);
    body["state"] = "running"; // the generated type is open; the closed set is enforced here
    EXPECT_NE(refusal(body).find("ServiceStateType"), std::string::npos);
    body.erase("state"); // Service_Create requires state
    refusal(body);
}

TEST(Tmf640Service, RefusesACategoryOtherThanRfs) {
    json body = valid_body();
    body["category"] = "CFS"; // A7
    EXPECT_NE(refusal(body).find("A7"), std::string::npos);
    body.erase("category");
    EXPECT_NE(refusal(body).find("A7"), std::string::npos);
}

TEST(Tmf640Service, RefusesASpecificationWithNoNetworkProfile) {
    json body = valid_body();
    body["serviceSpecification"]["id"] = "default"; // A1: exact key, no fallback
    EXPECT_NE(refusal(body).find("A1"), std::string::npos);
    body.erase("serviceSpecification"); // Service_Create requires it
    refusal(body);
}

TEST(Tmf640Service, RequiresAnImsiSupiAndADigitMsisdn) {
    json body = valid_body();
    body["serviceCharacteristic"] = json::array({characteristic("msisdn", "9997000123")});
    EXPECT_NE(refusal(body).find("A2"), std::string::npos);
    body["serviceCharacteristic"] = json::array({characteristic("supi", "nai-user@example.org")});
    EXPECT_NE(refusal(body).find("IMSI"), std::string::npos);
    body["serviceCharacteristic"] = json::array(
        {characteristic("supi", "imsi-999700000012345"), characteristic("msisdn", "+9997")});
    EXPECT_NE(refusal(body).find("msisdn"), std::string::npos);
    body["serviceCharacteristic"] = json::array({characteristic("supi", "imsi-999700000012345"),
                                                 characteristic("supi", "imsi-999700000012346")});
    EXPECT_NE(refusal(body).find("more than once"), std::string::npos);
}

TEST(Tmf640Service, RefusesSimCredentialsWithoutEchoingThem) {
    constexpr const char* kKey = "0f1e2d3c4b5a69788796a5b4c3d2e1f0";
    for (const char* name : {"K", "opc", "encPermanentKey", "SQN"}) {
        json body = valid_body();
        body["serviceCharacteristic"].push_back(characteristic(name, kKey)); // A3
        const auto detail = refusal(body);
        EXPECT_NE(detail.find("A3"), std::string::npos) << name;
        EXPECT_EQ(detail.find(kKey), std::string::npos) << name;
    }
}

TEST(Tmf640Service, RefusesMalformedJsonWithoutQuotingIt) {
    const auto v = validate_create("{\"state\": \"active\", \"k\": \"0f1e2d3c", known_profile);
    ASSERT_FALSE(v.has_value());
    EXPECT_EQ(v.error().status, 400);
    EXPECT_EQ(v.error().detail.find("0f1e2d3c"), std::string::npos);
}

// ---- PATCH /service/{id} (ADR-0576, mapping row A4): the pure planning step -------------------

namespace {

using provisioning::tmf640::NetworkAction;
using provisioning::tmf640::plan_patch;

json stored_in(const std::string& state) {
    auto v = valid_body();
    v["state"] = state;
    v["id"] = "svc-1";
    v["href"] = "/tmf-api/ServiceActivationAndConfiguration/v4/service/svc-1";
    return v;
}

} // namespace

TEST(Tmf640Patch, ActiveToInactiveBarsPacketServices) {
    const auto p = plan_patch(stored_in("active"), R"({"state":"inactive"})");
    ASSERT_TRUE(p.has_value()) << p.error().detail;
    EXPECT_EQ(p->action, NetworkAction::Bar);
    EXPECT_EQ(p->state, "inactive");
    EXPECT_EQ(p->supi, "imsi-999700000012345");
    EXPECT_EQ(p->msisdn, "9997000123");
    EXPECT_EQ(p->service["state"], "inactive");
    EXPECT_EQ(p->service["id"], "svc-1"); // merge keeps what the patch does not mention
}

TEST(Tmf640Patch, InactiveToActiveRewritesWithoutBarring) {
    const auto p = plan_patch(stored_in("inactive"), R"({"state":"active"})");
    ASSERT_TRUE(p.has_value()) << p.error().detail;
    EXPECT_EQ(p->action, NetworkAction::Activate);
}

TEST(Tmf640Patch, PreActiveToActiveWritesTheNetworkDocuments) {
    for (const char* from : {"feasibilityChecked", "designed", "reserved"}) {
        const auto p = plan_patch(stored_in(from), R"({"state":"active"})");
        ASSERT_TRUE(p.has_value()) << from;
        EXPECT_EQ(p->action, NetworkAction::Activate) << from;
    }
}

TEST(Tmf640Patch, MovesThatTouchNoSubscriberWriteNothing) {
    // A6: dates and free-form fields are stored only; a same-state PATCH and a move between
    // pre-active states or pre-active -> inactive never reach the UDR.
    auto p = plan_patch(stored_in("active"), R"({"endDate":"2027-01-01T00:00:00Z"})");
    ASSERT_TRUE(p.has_value()) << p.error().detail;
    EXPECT_EQ(p->action, NetworkAction::None);
    EXPECT_EQ(p->service["endDate"], "2027-01-01T00:00:00Z");
    EXPECT_EQ(p->service["startDate"], "2026-11-08T00:00:00Z"); // untouched

    p = plan_patch(stored_in("designed"), R"({"state":"reserved"})");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->action, NetworkAction::None);

    p = plan_patch(stored_in("designed"), R"({"state":"inactive"})");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->action, NetworkAction::None);
}

TEST(Tmf640Patch, MergePatchNullRemovesAMemberButNotARequiredOne) {
    auto p = plan_patch(stored_in("active"), R"({"startDate":null})"); // RFC 7396
    ASSERT_TRUE(p.has_value()) << p.error().detail;
    EXPECT_FALSE(p->service.contains("startDate"));
    EXPECT_FALSE(plan_patch(stored_in("active"), R"({"state":null})").has_value());
}

TEST(Tmf640Patch, RefusesWhatItCannotDoWithA400) {
    const auto active = stored_in("active");
    const auto refused = [&](const std::string& body, const json& stored = json()) {
        const auto p = plan_patch(stored.is_null() ? active : stored, body);
        EXPECT_FALSE(p.has_value()) << body;
        if (!p.has_value()) {
            EXPECT_EQ(p.error().status, 400) << body;
        }
    };
    refused(R"({"state":"terminated"})");                  // A5
    refused(R"({"state":"nonsense"})");                    // closed ServiceStateType set
    refused(R"({"category":"CFS"})");                      // A7
    refused(R"({"serviceSpecification":{"id":"other"}})"); // A1: identity of the profile
    refused(R"({"id":"x"})");
    refused(R"({"href":"/x"})");
    refused(R"({"state":"designed"})");                        // active -> pre-active
    refused(R"({"state":"reserved"})", stored_in("inactive")); // inactive -> pre-active
    refused(R"([1])");
    refused("{not json");
    // serviceCharacteristic replaces the array (RFC 7396), so supi/msisdn must be re-sent intact.
    refused(R"({"serviceCharacteristic":[]})");
    refused(R"({"serviceCharacteristic":[{"name":"supi","value":"imsi-999700000099999"}]})");
    refused(R"({"serviceCharacteristic":[{"name":"supi","value":"imsi-999700000012345"},)"
            R"({"name":"msisdn","value":"9997000123"},{"name":"opc","value":"x"}]})"); // A3
    refused(R"({"serviceCharacteristic":[{"name":"supi","value":"imsi-999700000012345"},)"
            R"({"name":"msisdn","value":"9997000123"},{"name":"supi","value":"x"}]})");
}

TEST(Tmf640Patch, RefusalNeverEchoesACharacteristicValue) {
    const auto p = plan_patch(
        stored_in("active"),
        R"({"serviceCharacteristic":[{"name":"opc","value":"SECRET-0123456789abcdef"}]})");
    ASSERT_FALSE(p.has_value());
    EXPECT_EQ(p.error().detail.find("SECRET"), std::string::npos);
}

// ADR-0585: DELETE bars a Service that has network documents at the UDR, and writes nothing for
// a pre-active one.
TEST(Tmf640Delete, ActiveAndInactiveServicesAreBarredFirst) {
    for (const char* state : {"active", "inactive"}) {
        const auto p = provisioning::tmf640::plan_delete(stored_in(state));
        EXPECT_TRUE(p.bar) << state;
        EXPECT_FALSE(p.supi.empty()) << state;
        EXPECT_FALSE(p.specification_id.empty()) << state;
    }
}

TEST(Tmf640Delete, PreActiveServicesWriteNothing) {
    for (const char* state : {"feasibilityChecked", "designed", "reserved"}) {
        EXPECT_FALSE(provisioning::tmf640::plan_delete(stored_in(state)).bar) << state;
    }
}

// ADR-0586: the pure parts of the TMF640 notifications.
TEST(Tmf640Events, ListenerPathIsTheSwaggerClientPath) {
    using namespace provisioning::tmf640;
    EXPECT_EQ(listener_path(kServiceCreateEvent), "/listener/serviceCreateEvent");
    EXPECT_EQ(listener_path(kServiceAttributeValueChangeEvent),
              "/listener/serviceAttributeValueChangeEvent");
    EXPECT_EQ(listener_path(kServiceStateChangeEvent), "/listener/serviceStateChangeEvent");
    EXPECT_EQ(listener_path(kServiceDeleteEvent), "/listener/serviceDeleteEvent");
}

TEST(Tmf640Events, EventCarriesTheServiceAndItsType) {
    const json service{{"id", "svc-1"}, {"state", "inactive"}};
    const auto e =
        provisioning::tmf640::make_event(provisioning::tmf640::kServiceStateChangeEvent, service);
    EXPECT_EQ(e.at("eventType"), "ServiceStateChangeEvent");
    EXPECT_EQ(e.at("event").at("service"), service);
    EXPECT_FALSE(e.at("eventId").get<std::string>().empty());
    EXPECT_FALSE(e.at("eventTime").get<std::string>().empty());
}

TEST(Tmf640Events, ListenerInputNeedsAnHttpsCallback) {
    using provisioning::tmf640::validate_listener;
    EXPECT_TRUE(validate_listener(R"({"callback":"https://bss.example/cb"})").has_value());
    const auto q = validate_listener(R"({"callback":"https://bss.example/cb","query":"x"})");
    ASSERT_TRUE(q.has_value());
    EXPECT_EQ(q->at("query"), "x");
    for (const char* bad : {R"({})",
                            R"({"callback":"http://bss.example/cb"})",
                            R"({"callback":42})",
                            R"({"callback":"https://bss.example/cb","query":1})",
                            R"([])",
                            "not json"}) {
        const auto r = validate_listener(bad);
        ASSERT_FALSE(r.has_value()) << bad;
        EXPECT_EQ(r.error().status, 400) << bad;
    }
}
