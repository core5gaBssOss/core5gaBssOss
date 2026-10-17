// REVIEW-5ae0ef1 SPEC-02/04: sbi_core::http2::apply_json_patch_body -- a PATCH body is the JSON
// Patch (RFC 6902) document the R19 YAMLs declare for "application/json-patch+json" (PatchItem[],
// minItems 1), with TS 29.500 5.2.7.2's 415 / 400 INVALID_MSG_FORMAT for anything else.

#include "sbi_core/json_body.hpp"

#include <nlohmann/json.hpp>

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
