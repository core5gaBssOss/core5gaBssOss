// Conformance tests for bss/roaming-interconnect's TMF651 error responses (ADR-0616, BSS SBI-03
// PR 3): the code its routes run (bss/roaming-interconnect/src/tmf_errors.hpp and
// tmf_core::parse_json_body<InterconnectAgreement>) answers the TM Forum `Error`, never 3GPP
// ProblemDetails.
//
// The Error definition is read at test time from the vendored TMF640 swagger
// (specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json:3405-3448), not from TMF651's:
// TMF651-Agreement-v4.0.0.swagger.json is vendored by ADR-0611 on a branch this one is not cut
// from. ADR-0611 records the two `Error` definitions as identical (compared as parsed JSON).

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

#include "store.hpp"
#include "tmf_core/error.hpp"
#include "tmf_errors.hpp"

#include <gtest/gtest.h>

using nlohmann::json;

namespace {

const json& error_definition() {
    static const json def = [] {
        std::ifstream in(TMF640_SWAGGER_PATH);
        return json::parse(in).at("definitions").at("Error");
    }();
    return def;
}

// Every member is a declared Error property, every declared-string one is a string, every
// required one is present, code/status are the HTTP status as a string (ADR-0612 Q2), and no 3GPP
// ProblemDetails member leaks in.
json expect_tmf_error(const sbi_core::http2::Response& r, int status) {
    EXPECT_EQ(r.status, status);
    EXPECT_EQ(r.headers.count("content-type"), 1u);
    const auto ct = r.headers.find("content-type");
    EXPECT_TRUE(ct != r.headers.end() && ct->second == "application/json;charset=utf-8");

    const json body = json::parse(r.body);
    EXPECT_TRUE(body.is_object());
    const json& def = error_definition();
    for (const auto& required : def.at("required")) {
        EXPECT_TRUE(body.contains(required.get<std::string>())) << required;
    }
    for (const auto& [name, value] : body.items()) {
        EXPECT_TRUE(def.at("properties").contains(name)) << "undeclared Error member: " << name;
        if (def.at("properties").contains(name) &&
            def.at("properties").at(name).value("type", "") == "string") {
            EXPECT_TRUE(value.is_string()) << name;
        }
    }
    for (const char* problem_member : {"cause", "title", "detail", "invalidParams"}) {
        EXPECT_FALSE(body.contains(problem_member)) << problem_member;
    }
    EXPECT_EQ(body.value("code", ""), std::to_string(status));
    EXPECT_EQ(body.value("status", ""), std::to_string(status));
    return body;
}

sbi_core::http2::Request post_agreement(std::string body) {
    sbi_core::http2::Request req;
    req.method = "POST";
    req.path = "/tmf-api/agreementManagement/v4/agreement";
    req.body = std::move(body);
    return req;
}

sbi_core::http2::Response parse_agreement(const sbi_core::http2::Request& req) {
    sbi_core::http2::Response err;
    EXPECT_FALSE(tmf_core::parse_json_body<roaming_interconnect::InterconnectAgreement>(req, err)
                     .has_value());
    return err;
}

} // namespace

TEST(RoamingInterconnectTmfError, StoreRejectionIs400TmfErrorWithStoreMessage) {
    const auto handler = roaming_interconnect::guarded(
        [](const sbi_core::http2::Request&) -> sbi_core::http2::Response {
            throw roaming_interconnect::InvalidRequest("a referenced entity does not exist");
        });
    const json body = expect_tmf_error(handler(post_agreement("{}")), 400);
    EXPECT_EQ(body.at("reason"), "Bad Request");
    EXPECT_EQ(body.at("message"), "a referenced entity does not exist");
}

TEST(RoamingInterconnectTmfError, GuardedPassesASuccessfulResponseThrough) {
    const auto handler = roaming_interconnect::guarded(
        [](const sbi_core::http2::Request&) { return sbi_core::http2::Response::json(200, "[]"); });
    const auto r = handler(post_agreement("{}"));
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.body, "[]");
}

TEST(RoamingInterconnectTmfError, GuardedLeavesOtherExceptionsToTheFramework) {
    // Anything but InvalidRequest is not a client error: it propagates to sbi-core's 500 (PR 7).
    const auto handler = roaming_interconnect::guarded(
        [](const sbi_core::http2::Request&) -> sbi_core::http2::Response {
            throw std::runtime_error("database down");
        });
    EXPECT_THROW((void)handler(post_agreement("{}")), std::runtime_error);
}

TEST(RoamingInterconnectTmfError, UnknownAgreementIs404TmfError) {
    const json body = expect_tmf_error(roaming_interconnect::agreement_not_found("42"), 404);
    EXPECT_EQ(body.at("reason"), "Not Found");
    EXPECT_EQ(body.at("message"), "No InterconnectAgreement 42");
    EXPECT_EQ(body.size(), 4u);
}

TEST(RoamingInterconnectTmfError, MalformedAgreementBodyIs400TmfErrorWithoutEchoingInput) {
    const auto err = parse_agreement(post_agreement(R"({"name": "SECRET-INPUT-TOKEN",)"));
    expect_tmf_error(err, 400);
    EXPECT_EQ(err.body.find("SECRET-INPUT-TOKEN"), std::string::npos);
}

TEST(RoamingInterconnectTmfError, WrongTypedAgreementMemberIs400TmfError) {
    // bss_sid::Agreement reads `name` as a string.
    const auto err = parse_agreement(post_agreement(R"({"name": 7})"));
    expect_tmf_error(err, 400);
}

TEST(RoamingInterconnectTmfError, WellFormedAgreementBodyParses) {
    sbi_core::http2::Response err;
    const auto parsed = tmf_core::parse_json_body<roaming_interconnect::InterconnectAgreement>(
        post_agreement(R"({"name": "MNO-A / MNO-B", "partnerOperatorPlmnId": "00101"})"), err);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->agreement.name, "MNO-A / MNO-B");
    EXPECT_EQ(parsed->partnerOperatorPlmnId, "00101");
    EXPECT_TRUE(err.body.empty());
}
