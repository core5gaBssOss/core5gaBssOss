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

// ADR-0576: PATCH /service/{id}. Every row of the transition table in plan_patch, plus the A-row
// checks re-run on the merged Service.
namespace {

using provisioning::tmf640::plan_patch;
using provisioning::tmf640::UdrAction;

json stored_with_state(const std::string& state) {
    json s = valid_body();
    s["state"] = state;
    s["id"] = "svc-1";
    s["href"] = "/tmf-api/ServiceActivationAndConfiguration/v4/service/svc-1";
    return s;
}

provisioning::tmf640::PatchPlan
planned(const std::string& from, const json& patch, const std::string& ct = "application/json") {
    auto p = plan_patch(stored_with_state(from), ct, patch.dump(), known_profile);
    EXPECT_TRUE(p.has_value()) << from << " " << patch.dump() << ": "
                               << (p.has_value() ? "" : p.error().detail);
    return p.has_value() ? *p : provisioning::tmf640::PatchPlan{};
}

int refused(const std::string& from,
            const json& patch,
            const std::string& ct = "application/json") {
    auto p = plan_patch(stored_with_state(from), ct, patch.dump(), known_profile);
    EXPECT_FALSE(p.has_value()) << from << " " << patch.dump();
    return p.has_value() ? 0 : p.error().status;
}

} // namespace

TEST(Tmf640Patch, PreActiveToPreActiveStoresOnly) {
    const auto p = planned("reserved", {{"state", "designed"}});
    EXPECT_EQ(p.action, UdrAction::None);
    EXPECT_TRUE(p.state_changed);
    EXPECT_EQ(p.service["state"], "designed");
}

TEST(Tmf640Patch, PreActiveToActiveIsAFullUnbarredWrite) {
    const auto p = planned("reserved", {{"state", "active"}});
    EXPECT_EQ(p.action, UdrAction::FullWrite);
    EXPECT_FALSE(p.barred);
}

TEST(Tmf640Patch, PreActiveToInactiveIsAFullBarredWrite) {
    const auto p = planned("designed", {{"state", "inactive"}});
    EXPECT_EQ(p.action, UdrAction::FullWrite);
    EXPECT_TRUE(p.barred);
}

TEST(Tmf640Patch, ActiveInactiveSwapsOnlyTheAmDataBarring) {
    const auto off = planned("active", {{"state", "inactive"}});
    EXPECT_EQ(off.action, UdrAction::AmData);
    EXPECT_TRUE(off.barred);
    EXPECT_TRUE(off.state_changed);
    EXPECT_TRUE(off.changed.empty());
    const auto on = planned("inactive", {{"state", "active"}});
    EXPECT_EQ(on.action, UdrAction::AmData);
    EXPECT_FALSE(on.barred);
}

TEST(Tmf640Patch, ProvisionedBackToPreActiveIsRefused) {
    EXPECT_EQ(refused("active", {{"state", "reserved"}}), 400);
    EXPECT_EQ(refused("inactive", {{"state", "feasibilityChecked"}}), 400);
}

TEST(Tmf640Patch, TerminatedIsRefused) {
    EXPECT_EQ(refused("active", {{"state", "terminated"}}), 400);
}

TEST(Tmf640Patch, MsisdnChangeRewritesProvisionedDataAndKeepsTheBarring) {
    const json chars = json::array(
        {characteristic("supi", "imsi-999700000012345"), characteristic("msisdn", "9997000999")});
    const auto barred = planned("inactive", {{"serviceCharacteristic", chars}});
    EXPECT_EQ(barred.action, UdrAction::FullWrite);
    EXPECT_TRUE(barred.barred);
    EXPECT_EQ(barred.validated.msisdn, "9997000999");
    EXPECT_FALSE(barred.state_changed);
    EXPECT_EQ(barred.changed, std::vector<std::string>{"serviceCharacteristic"});
    EXPECT_EQ(planned("active", {{"serviceCharacteristic", chars}}).action, UdrAction::FullWrite);
    // Pre-active: nothing is provisioned, so the new MSISDN is stored only.
    EXPECT_EQ(planned("reserved", {{"serviceCharacteristic", chars}}).action, UdrAction::None);
}

TEST(Tmf640Patch, NoOpAndStoreOnlyFieldsWriteNothing) {
    const auto same = planned("active", {{"state", "active"}});
    EXPECT_EQ(same.action, UdrAction::None);
    EXPECT_FALSE(same.state_changed);
    EXPECT_TRUE(same.changed.empty());
    const auto named = planned("active", {{"name", "data plan"}});
    EXPECT_EQ(named.action, UdrAction::None);
    EXPECT_EQ(named.changed, std::vector<std::string>{"name"});
}

TEST(Tmf640Patch, ReRunsTheARowChecksOnTheMergedService) {
    // A merge patch replaces the whole array: dropping supi is refused, not silently stored.
    EXPECT_EQ(
        refused("active",
                {{"serviceCharacteristic", json::array({characteristic("msisdn", "9997000999")})}}),
        400);
    // A changed supi.
    EXPECT_EQ(refused("active",
                      {{"serviceCharacteristic",
                        json::array({characteristic("supi", "imsi-999700000099999")})}}),
              400);
    // A3: a key smuggled in by PATCH is refused without being echoed.
    auto p =
        plan_patch(stored_with_state("active"),
                   "application/json",
                   json{{"serviceCharacteristic",
                         json::array({characteristic("supi", "imsi-999700000012345"),
                                      characteristic("OPc", "000102030405060708090a0b0c0d0e0f")})}}
                       .dump(),
                   known_profile);
    ASSERT_FALSE(p.has_value());
    EXPECT_EQ(p.error().detail.find("000102030405"), std::string::npos);
    // A7 and A1 on the merged result; serviceSpecification.id cannot move.
    EXPECT_EQ(refused("active", {{"category", "CFS"}}), 400);
    EXPECT_EQ(refused("active", {{"serviceSpecification", {{"id", "rfs-unknown"}}}}), 400);
}

TEST(Tmf640Patch, RefusesNonServiceUpdateMembersAndOtherMediaTypes) {
    EXPECT_EQ(refused("active", {{"id", "other"}}), 400);
    EXPECT_EQ(refused("active", {{"href", "/x"}}), 400);
    EXPECT_EQ(refused("active", {{"state", "inactive"}}, "application/json-patch+json"), 400);
    EXPECT_EQ(refused("active", {{"state", "inactive"}}, "text/plain"), 400);
    EXPECT_EQ(refused("active", json::array()), 400);
    // Parameters on the media type are fine (the swagger's own is application/json;charset=utf-8).
    EXPECT_EQ(planned("active", {{"state", "inactive"}}, "application/json; charset=utf-8").action,
              UdrAction::AmData);
}
