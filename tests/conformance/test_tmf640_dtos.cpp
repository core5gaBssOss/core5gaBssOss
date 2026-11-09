// Round-trip tests for the TMF640 DTOs generated from
// specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json (ADR-0558, ADR-0568). Same precedent
// as test_bsf_dtos.cpp: real generated types, JSON in -> struct -> JSON out. The field names and
// values below are the ones docs/PROVISIONING_MAPPING.md Part A maps; the swagger is the source.
// The characteristic names follow mapping row A2 (3GPP attribute names: `supi`, and `nssai` of
// TS29503_Nudm_SDM.yaml AccessAndMobilitySubscriptionData).

#include <nlohmann/json.hpp>

#include "TMF640_ServiceActivation_v4_0_0.hpp"

#include <gtest/gtest.h>

using nlohmann::json;

TEST(Tmf640Dtos, ServiceRoundTripsWithPolymorphismMembersAndCharacteristics) {
    const json in = json::parse(R"({
        "id": "svc-1",
        "href": "/tmf-api/ServiceActivationAndConfiguration/v4/service/svc-1",
        "category": "RFS",
        "state": "active",
        "serviceSpecification": {"id": "spec-5g-data", "@referredType": "ServiceSpecification"},
        "serviceCharacteristic": [
            {"name": "supi", "valueType": "string", "value": "imsi-001010000000001"},
            {"name": "nssai", "valueType": "object",
             "value": {"defaultSingleNssais": [{"sst": 1, "sd": "000001"}]}}
        ],
        "@type": "Service",
        "@baseType": "Entity"
    })");

    const auto service = in.get<tmf_gen::Service>();
    ASSERT_TRUE(service.state.has_value());
    EXPECT_EQ(service.state->value, tmf_gen::ServiceStateType::active);
    ASSERT_TRUE(service.atType.has_value());
    EXPECT_EQ(*service.atType, "Service");
    ASSERT_TRUE(service.serviceCharacteristic.has_value());
    ASSERT_EQ(service.serviceCharacteristic->size(), 2u);
    EXPECT_EQ((*service.serviceCharacteristic)[0].name, "supi");
    // Characteristic.value is the swagger's `Any`: a structured value survives unchanged
    // (mapping row B7's encoding relies on this).
    EXPECT_EQ((*service.serviceCharacteristic)[1].value.at("defaultSingleNssais").at(0).at("sd"),
              "000001");

    const json out = service;
    EXPECT_EQ(out, in);
}

TEST(Tmf640Dtos, KeywordEnumValueKeepsItsWireName) {
    // OrderItemActionType's value `delete` is a C++ keyword: the constant is `delete_`, the wire
    // value stays "delete".
    EXPECT_EQ(tmf_gen::OrderItemActionType::delete_, "delete");
    const json j = tmf_gen::OrderItemActionType{tmf_gen::OrderItemActionType::delete_};
    EXPECT_EQ(j, "delete");
    EXPECT_EQ(json("inactive").get<tmf_gen::ServiceStateType>().value,
              tmf_gen::ServiceStateType::inactive);
}
