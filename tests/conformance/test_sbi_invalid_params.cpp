// ADR-0607 (REVIEW-5ae0ef1 SBI-03, step 1): ProblemDetails.invalidParams from sbi-core.
// TS 29.500 V19.7.0 Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the
// 'ProblemDetails' data structure indicating unsupported, missing or incorrect IE(s) or query
// parameter(s) or 3gpp-Sbi-Discovery-* header(s)." Field names and the four encodings of `param`
// are TS29571_CommonData.yaml's (ProblemDetails.invalidParams: array of InvalidParam, minItems 1;
// InvalidParam: param (string, required), reason (string)).

#include "sbi_core/json_body.hpp"
#include "sbi_core/problem_details.hpp"

#include <nlohmann/json.hpp>

#include <gtest/gtest.h>

namespace {

using nlohmann::json;
using sbi_core::http2::problem_response;

} // namespace

TEST(SbiInvalidParams, ProblemResponseWritesInvalidParamsWithTheYamlFieldNames) {
    const auto r =
        problem_response(400,
                         "Bad Request",
                         "unsupported query parameter",
                         "INVALID_QUERY_PARAM",
                         {sbi_core::invalid_query_param("foo", "not supported"),
                          sbi_core::invalid_header("3gpp-Sbi-Discovery-target-nf-type")});
    EXPECT_EQ(r.status, 400);
    EXPECT_EQ(r.headers.find("content-type")->second, "application/problem+json");
    const auto body = json::parse(r.body);
    EXPECT_EQ(body, json::parse(R"({
        "status": 400,
        "title": "Bad Request",
        "detail": "unsupported query parameter",
        "cause": "INVALID_QUERY_PARAM",
        "invalidParams": [
            {"param": "query foo", "reason": "not supported"},
            {"param": "header 3gpp-Sbi-Discovery-target-nf-type"}
        ]
    })"));
}

// Existing callers: the four-argument overload's body is byte-for-byte what it was, and the
// five-argument overload with an empty list writes that same body (no empty invalidParams array,
// which the YAML's minItems 1 forbids).
TEST(SbiInvalidParams, NoInvalidParamsLeavesTheBodyUnchanged) {
    const auto four = problem_response(404, "Not Found", "x", "SUBSCRIPTION_NOT_FOUND");
    EXPECT_EQ(
        four.body,
        R"({"cause":"SUBSCRIPTION_NOT_FOUND","detail":"x","status":404,"title":"Not Found"})");
    const auto five = problem_response(404, "Not Found", "x", "SUBSCRIPTION_NOT_FOUND", {});
    EXPECT_EQ(five.body, four.body);
    EXPECT_EQ(five.headers, four.headers);
    const auto no_cause = problem_response(404, "Not Found", "x", std::nullopt);
    EXPECT_EQ(no_cause.body, R"({"detail":"x","status":404,"title":"Not Found"})");
    EXPECT_FALSE(json(sbi_core::make_problem_details(400, "t", "d", "MANDATORY_IE_MISSING", {}))
                     .contains("invalidParams"));
}

TEST(SbiInvalidParams, ParamEncodingsFollowTheInvalidParamDescription) {
    // A JSON body attribute: a JSON Pointer, with RFC 6901 escaping of '~' and '/'.
    EXPECT_EQ(sbi_core::invalid_body_ie(json::json_pointer("/snssai/sst")).param, "/snssai/sst");
    EXPECT_EQ(sbi_core::invalid_body_ie(json::json_pointer("/a~1b/c~0d")).param, "/a~1b/c~0d");
    EXPECT_EQ(sbi_core::invalid_body_ie(json::json_pointer("/dnn") / "x/y").param, "/dnn/x~1y");
    // An HTTP header, a query parameter, a path variable.
    EXPECT_EQ(sbi_core::invalid_header("3gpp-Sbi-Callback").param, "header 3gpp-Sbi-Callback");
    EXPECT_EQ(sbi_core::invalid_query_param("dataset-names").param, "query dataset-names");
    EXPECT_EQ(sbi_core::invalid_path_variable("ueId").param, "{ueId}");
    // reason is optional and omitted from the JSON when absent.
    EXPECT_FALSE(sbi_core::invalid_path_variable("ueId").reason.has_value());
    EXPECT_EQ(json(sbi_core::invalid_path_variable("ueId")), json::parse(R"({"param":"{ueId}"})"));
    EXPECT_EQ(json(sbi_core::invalid_path_variable("ueId", "not a SUPI")),
              json::parse(R"({"param":"{ueId}","reason":"not a SUPI"})"));
}

TEST(SbiInvalidParams, RoundTripsThroughProblemDetails) {
    const auto r = problem_response(400,
                                    "Bad Request",
                                    "d",
                                    "MANDATORY_IE_MISSING",
                                    {sbi_core::invalid_body_ie(json::json_pointer("/dnn"))});
    const auto pd = json::parse(r.body).get<sbi_core::ProblemDetails>();
    ASSERT_TRUE(pd.invalid_params.has_value());
    ASSERT_EQ(pd.invalid_params->size(), 1u);
    EXPECT_EQ((*pd.invalid_params)[0].param, "/dnn");
    EXPECT_FALSE((*pd.invalid_params)[0].reason.has_value());
}

// The eight Table 5.2.7.2-1 rows whose description carries "(NOTE 1)", and nothing else.
TEST(SbiInvalidParams, Note1CausesAreExactlyTheTableRows) {
    for (const char* c : {"INVALID_QUERY_PARAM",
                          "MANDATORY_QUERY_PARAM_INCORRECT",
                          "OPTIONAL_QUERY_PARAM_INCORRECT",
                          "MANDATORY_QUERY_PARAM_MISSING",
                          "MANDATORY_IE_INCORRECT",
                          "OPTIONAL_IE_INCORRECT",
                          "MANDATORY_IE_MISSING",
                          "MISSING_PARAMETER"}) {
        EXPECT_TRUE(sbi_core::table_5272_note1_applies(c)) << c;
    }
    for (const char* c : {"INVALID_MSG_FORMAT",
                          "INVALID_API",
                          "UNSPECIFIED_MSG_FAILURE",
                          "ACCESS_TOKEN_CLAIM_MISSING",
                          "SUBSCRIPTION_NOT_FOUND",
                          "",
                          "mandatory_ie_missing"}) {
        EXPECT_FALSE(sbi_core::table_5272_note1_applies(c)) << c;
    }
}
