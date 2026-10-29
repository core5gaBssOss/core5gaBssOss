// REVIEW-5ae0ef1 SPEC-02/04: sbi_core::http2::apply_json_patch_body -- a PATCH body is the JSON
// Patch (RFC 6902) document the R19 YAMLs declare for "application/json-patch+json" (PatchItem[],
// minItems 1), with TS 29.500 5.2.7.2's 415 / 400 INVALID_MSG_FORMAT for anything else.

#include "sbi_core/json_body.hpp"

#include <nlohmann/json.hpp>

#include "TS29521_Nbsf_Management.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;
using sbi_core::http2::apply_json_patch_body;
using sbi_core::http2::Request;

Request patch_request(const std::string& content_type, const std::string& body) {
    Request req;
    req.method = "PATCH";
    req.path = "/x";
    if (!content_type.empty()) {
        req.headers.emplace("content-type", content_type);
    }
    req.body = body;
    return req;
}

std::string cause_of(const sbi_core::http2::Response& r) {
    return json::parse(r.body).value("cause", "");
}

const json kTarget = {{"maximumNumberOfReports", 1}, {"monitoringType", "LOCATION_REPORTING"}};

} // namespace

TEST(SbiJsonPatch, APatchItemArrayUpdatesTheField) {
    const auto out = apply_json_patch_body(
        patch_request("application/json-patch+json; charset=utf-8",
                      R"([{"op":"replace","path":"/maximumNumberOfReports","value":5}])"),
        kTarget);
    ASSERT_TRUE(out.has_value()) << out.error().body;
    EXPECT_EQ(out->at("maximumNumberOfReports"), 5);
    EXPECT_EQ(out->at("monitoringType"), "LOCATION_REPORTING");
}

TEST(SbiJsonPatch, AnotherContentTypeIs415WithAcceptPatch) {
    const auto out = apply_json_patch_body(
        patch_request("application/merge-patch+json", R"({"maximumNumberOfReports":5})"), kTarget);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().status, 415);
    const auto it = out.error().headers.find("accept-patch");
    ASSERT_NE(it, out.error().headers.end());
    EXPECT_EQ(it->second, "application/json-patch+json");
}

TEST(SbiJsonPatch, AMergePatchObjectEmptyArrayMalformedOrRepeatedKeyIs400InvalidMsgFormat) {
    for (const std::string body :
         {R"({"maximumNumberOfReports":5})", "[]", "[{", R"([{"op":"replace","op":"add"}])"}) {
        const auto out =
            apply_json_patch_body(patch_request("application/json-patch+json", body), kTarget);
        ASSERT_FALSE(out.has_value()) << body;
        EXPECT_EQ(out.error().status, 400) << body;
        EXPECT_EQ(cause_of(out.error()), "INVALID_MSG_FORMAT") << body;
    }
}

TEST(SbiJsonPatch, APatchThatCannotBeAppliedIs400) {
    const auto out = apply_json_patch_body(
        patch_request("application/json-patch+json",
                      R"([{"op":"replace","path":"/no/such/member","value":1}])"),
        kTarget);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().status, 400);
    EXPECT_EQ(cause_of(out.error()), "INVALID_MSG_FORMAT");
}

// REVIEW-5ae0ef1 SBI-03: the parse helpers' 400s carry the TS 29.500 Table 5.2.7.2-1 cause.
// PcfBinding's mandatory members are dnn and snssai (TS29521_Nbsf_Management.yaml).
TEST(SbiJsonBody, ParseFailuresCarryTheirTable5272Cause) {
    using sbi_core::http2::parse_json_body;
    const auto cause_for = [](const std::string& body) {
        sbi_core::http2::Response err;
        EXPECT_FALSE(
            parse_json_body<sbi_gen::PcfBinding>(patch_request("application/json", body), err)
                .has_value());
        EXPECT_EQ(err.status, 400);
        return cause_of(err);
    };
    EXPECT_EQ(cause_for(R"({"dnn":)"), "INVALID_MSG_FORMAT");
    EXPECT_EQ(cause_for(R"({"snssai":{"sst":1}})"), "MANDATORY_IE_MISSING");
    EXPECT_EQ(cause_for(R"({"dnn":7,"snssai":{"sst":1}})"), "MANDATORY_IE_INCORRECT");
}

TEST(SbiJsonBody, ProblemResponseWritesTheCauseOrNone) {
    const auto with =
        sbi_core::http2::problem_response(404, "Not Found", "x", "SUBSCRIPTION_NOT_FOUND");
    EXPECT_EQ(cause_of(with), "SUBSCRIPTION_NOT_FOUND");
    EXPECT_EQ(with.headers.find("content-type")->second, "application/problem+json");
    const auto without = sbi_core::http2::problem_response(404, "Not Found", "x", std::nullopt);
    EXPECT_FALSE(json::parse(without.body).contains("cause"));
}
