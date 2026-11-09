// ADR-0569: TMF640 POST /service validation (bss/provisioning/src/tmf640_service.cpp) against the
// rows of docs/PROVISIONING_MAPPING.md Part A that the user approved on 2026-11-07 (ADR-0558).
// Pure checks: no database, no network.

#include <nlohmann/json.hpp>

#include <string>

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
