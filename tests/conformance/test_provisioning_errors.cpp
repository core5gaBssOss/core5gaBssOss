// ADR-0615 (BSS SBI-03 PR 2): the error bodies bss/provisioning sends.
// - TMF640 (/tmf-api/ServiceActivationAndConfiguration/v4): the TM Forum Error built by
//   libs/tmf-core (ADR-0612), checked against `definitions.Error` read at test time from the
//   vendored specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json, as
//   tests/conformance/test_tmf_error.cpp does for the helper itself.
// - The project-internal order API (/provisioning/v1/customerOrder): ProblemDetails with no
//   TS 29.500 cause (ADR-0612 Q5).
// Pure checks: no database, no network.

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

#include "order_api_errors.hpp"
#include "tmf640_events.hpp"
#include "tmf640_service.hpp"

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

std::string content_type(const sbi_core::http2::Response& r) {
    const auto it = r.headers.find("content-type");
    return it == r.headers.end() ? std::string{} : it->second;
}

// The TMF640 Error shape, with the ADR-0612 member values.
void expect_tmf640_error(const sbi_core::http2::Response& r,
                         int status,
                         const std::string& reason,
                         const std::string& message) {
    EXPECT_EQ(r.status, status);
    EXPECT_EQ(content_type(r), "application/json;charset=utf-8");
    const json body = json::parse(r.body);
    ASSERT_TRUE(body.is_object());
    const json& def = error_definition();
    for (const auto& required : def.at("required")) {
        EXPECT_TRUE(body.contains(required.get<std::string>())) << required;
    }
    for (const auto& [name, value] : body.items()) {
        EXPECT_TRUE(def.at("properties").contains(name)) << "not a TMF640 Error member: " << name;
    }
    EXPECT_EQ(body.at("code"), std::to_string(status));
    EXPECT_EQ(body.at("reason"), reason);
    EXPECT_EQ(body.at("message"), message);
    EXPECT_EQ(body.at("status"), std::to_string(status));
    EXPECT_FALSE(body.contains("referenceError"));
    for (const char* problem_member : {"cause", "title", "detail"}) {
        EXPECT_FALSE(body.contains(problem_member)) << problem_member;
    }
}

// ProblemDetails with no 3GPP cause and no TMF Error member.
void expect_problem_without_cause(const sbi_core::http2::Response& r,
                                  int status,
                                  const std::string& title,
                                  const std::string& detail) {
    EXPECT_EQ(r.status, status);
    EXPECT_EQ(content_type(r), "application/problem+json");
    const json body = json::parse(r.body);
    ASSERT_TRUE(body.is_object());
    EXPECT_EQ(body.at("status"), status);
    EXPECT_EQ(body.at("title"), title);
    EXPECT_EQ(body.at("detail"), detail);
    EXPECT_FALSE(body.contains("cause"));
    EXPECT_FALSE(body.contains("invalidParams"));
    for (const char* tmf_member : {"code", "reason", "message", "error"}) {
        EXPECT_FALSE(body.contains(tmf_member)) << tmf_member;
    }
}

} // namespace

TEST(ProvisioningTmf640Error, EveryServiceErrorStatusIsATmf640Error) {
    // The statuses tmf640_service.cpp and tmf640_events.cpp produce.
    using provisioning::tmf640::error_response;
    using provisioning::tmf640::ServiceError;
    expect_tmf640_error(
        error_response(ServiceError{400, "Bad Request", "d400"}), 400, "Bad Request", "d400");
    expect_tmf640_error(error_response(ServiceError{404, "Not Found", "no Service with this id"}),
                        404,
                        "Not Found",
                        "no Service with this id");
    expect_tmf640_error(error_response(ServiceError{
                            409, "Conflict", "the listener limit of this server is reached"}),
                        409,
                        "Conflict",
                        "the listener limit of this server is reached");
    expect_tmf640_error(
        error_response(
            ServiceError{500, "Internal Server Error", "the UDR write failed; nothing was stored"}),
        500,
        "Internal Server Error",
        "the UDR write failed; nothing was stored");
}

TEST(ProvisioningTmf640Error, ReasonComesFromTheStatusNotTheTitle) {
    const auto r =
        provisioning::tmf640::error_response({409, "free text that is not a reason phrase", "m"});
    expect_tmf640_error(r, 409, "Conflict", "m");
}

TEST(ProvisioningTmf640Error, RefusedCreateAndListenerAnswerTheTmf640Error) {
    const auto create =
        provisioning::tmf640::validate_create("{not json", [](const std::string&) { return true; });
    ASSERT_FALSE(create.has_value());
    expect_tmf640_error(provisioning::tmf640::error_response(create.error()),
                        400,
                        "Bad Request",
                        "request body is not valid JSON");

    const auto listener = provisioning::tmf640::validate_listener(R"({"callback":42})");
    ASSERT_FALSE(listener.has_value());
    const auto r = provisioning::tmf640::error_response(listener.error());
    expect_tmf640_error(r, 400, "Bad Request", listener.error().detail);
}

TEST(ProvisioningOrderApiError, MalformedBodyIsProblemDetailsWithoutCause) {
    expect_problem_without_cause(provisioning::order_api::malformed_body(),
                                 400,
                                 "Malformed JSON",
                                 "request body is not valid JSON");
}

TEST(ProvisioningOrderApiError, RejectedOrderIsProblemDetailsWithoutCause) {
    expect_problem_without_cause(provisioning::order_api::order_rejected("supi is required"),
                                 400,
                                 "Bad Request",
                                 "supi is required");
}

TEST(ProvisioningOrderApiError, UnknownOrderIsProblemDetailsWithoutCause) {
    // Was the ad-hoc {"error": "order not found"} before ADR-0615.
    expect_problem_without_cause(
        provisioning::order_api::order_not_found(), 404, "Not Found", "order not found");
}
