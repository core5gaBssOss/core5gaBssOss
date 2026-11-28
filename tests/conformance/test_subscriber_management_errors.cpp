// Conformance tests for bss/subscriber-management's error responses (ADR-0620, BSS SBI-03 PR 6):
// the code its routes run (bss/subscriber-management/src/api_errors.hpp and
// tmf_core::parse_json_body<bss_sid::Individual / Organization>).
// - TMF632 Party Management (/tmf-api/party/v4): the TM Forum `Error`, never 3GPP ProblemDetails.
// - The project-internal Account/Subscriber API (/bss-api/subscriberManagement/v1): ProblemDetails
//   with no TS 29.500 cause (ADR-0612 Q5).
//
// The Error definition is read at test time from the vendored TMF640 swagger
// (specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json:3405-3448), not from TMF632's:
// TMF632-Party-v4.0.0.swagger.json is vendored by ADR-0611 on a branch this one is not cut from.
// ADR-0611 records the two `Error` definitions as identical (compared as parsed JSON).
// Pure checks: no database, no network.

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

#include "api_errors.hpp"
#include "bss_sid/party.hpp"
#include "tmf_core/error.hpp"

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
    EXPECT_EQ(r.headers.count("content-type"), 1u);
    const auto it = r.headers.find("content-type");
    return it == r.headers.end() ? std::string{} : it->second;
}

// Every member is a declared Error property, every declared-string one is a string, every
// required one is present, code/status are the HTTP status as a string (ADR-0612 Q2), and no 3GPP
// ProblemDetails member leaks in.
json expect_tmf_error(const sbi_core::http2::Response& r, int status) {
    EXPECT_EQ(r.status, status);
    EXPECT_EQ(content_type(r), "application/json;charset=utf-8");

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
    EXPECT_FALSE(body.contains("referenceError"));
    EXPECT_EQ(body.value("code", ""), std::to_string(status));
    EXPECT_EQ(body.value("status", ""), std::to_string(status));
    return body;
}

// ProblemDetails with no 3GPP cause and no TMF Error member.
json expect_problem_without_cause(const sbi_core::http2::Response& r,
                                  int status,
                                  const std::string& title) {
    EXPECT_EQ(r.status, status);
    EXPECT_EQ(content_type(r), "application/problem+json");
    const json body = json::parse(r.body);
    EXPECT_TRUE(body.is_object());
    EXPECT_EQ(body.value("status", 0), status);
    EXPECT_EQ(body.value("title", ""), title);
    EXPECT_TRUE(body.contains("detail"));
    EXPECT_FALSE(body.contains("cause"));
    EXPECT_FALSE(body.contains("invalidParams"));
    for (const char* tmf_member : {"code", "reason", "message", "error"}) {
        EXPECT_FALSE(body.contains(tmf_member)) << tmf_member;
    }
    return body;
}

sbi_core::http2::Request post(std::string path, std::string body) {
    sbi_core::http2::Request req;
    req.method = "POST";
    req.path = std::move(path);
    req.body = std::move(body);
    return req;
}

template <typename T> sbi_core::http2::Response tmf_parse_error(std::string body) {
    sbi_core::http2::Response err;
    EXPECT_FALSE(tmf_core::parse_json_body<T>(post("/tmf-api/party/v4/x", std::move(body)), err)
                     .has_value());
    return err;
}

template <typename T> sbi_core::http2::Response internal_parse_error(std::string body) {
    sbi_core::http2::Response err;
    EXPECT_FALSE(subscriber_management::parse_internal_json_body<T>(
                     post("/bss-api/subscriberManagement/v1/x", std::move(body)), err)
                     .has_value());
    return err;
}

template <typename Exception> auto throwing(const char* what) {
    return [what](const sbi_core::http2::Request&) -> sbi_core::http2::Response {
        throw Exception(what);
    };
}

} // namespace

// --- TMF632 ---

TEST(SubscriberManagementTmf632Error, StoreRejectionIs400TmfErrorWithStoreMessage) {
    const auto handler = subscriber_management::guarded_tmf(
        throwing<subscriber_management::InvalidRequest>("a referenced entity does not exist"));
    const json body = expect_tmf_error(handler(post("/tmf-api/party/v4/individual", "{}")), 400);
    EXPECT_EQ(body.at("reason"), "Bad Request");
    EXPECT_EQ(body.at("message"), "a referenced entity does not exist");
}

TEST(SubscriberManagementTmf632Error, StoreConflictIs409TmfError) {
    // TMF632 declares 409 on POST /individual and POST /organization.
    const auto handler = subscriber_management::guarded_tmf(
        throwing<subscriber_management::Conflict>("an identifier is already in use"));
    const json body = expect_tmf_error(handler(post("/tmf-api/party/v4/organization", "{}")), 409);
    EXPECT_EQ(body.at("reason"), "Conflict");
    EXPECT_EQ(body.at("message"), "an identifier is already in use");
}

TEST(SubscriberManagementTmf632Error, GuardedTmfPassesSuccessAndLeavesOtherExceptions) {
    const auto ok = subscriber_management::guarded_tmf(
        [](const sbi_core::http2::Request&) { return sbi_core::http2::Response::json(200, "[]"); });
    const auto r = ok(post("/tmf-api/party/v4/individual", "{}"));
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.body, "[]");
    // Anything but InvalidRequest/Conflict propagates to sbi-core's 500 (PR 7).
    const auto other = subscriber_management::guarded_tmf(throwing<std::runtime_error>("db down"));
    EXPECT_THROW((void)other(post("/tmf-api/party/v4/individual", "{}")), std::runtime_error);
}

TEST(SubscriberManagementTmf632Error, UnknownPartyIs404TmfError) {
    for (const std::string kind : {"Individual", "Organization"}) {
        const json body = expect_tmf_error(subscriber_management::party_not_found(kind, "42"), 404);
        EXPECT_EQ(body.at("reason"), "Not Found");
        EXPECT_EQ(body.at("message"), "No " + kind + " 42");
        EXPECT_EQ(body.size(), 4u);
    }
}

TEST(SubscriberManagementTmf632Error, MalformedPartyBodyIs400TmfErrorWithoutEchoingInput) {
    const auto ind = tmf_parse_error<bss_sid::Individual>(R"({"givenName": "SECRET-INPUT-TOKEN",)");
    expect_tmf_error(ind, 400);
    EXPECT_EQ(ind.body.find("SECRET-INPUT-TOKEN"), std::string::npos);
    const auto org = tmf_parse_error<bss_sid::Organization>(R"({"name": "SECRET-INPUT-TOKEN",)");
    expect_tmf_error(org, 400);
    EXPECT_EQ(org.body.find("SECRET-INPUT-TOKEN"), std::string::npos);
}

TEST(SubscriberManagementTmf632Error, WrongTypedPartyMemberIs400TmfError) {
    // bss_sid reads Individual.givenName and Organization.name as strings.
    expect_tmf_error(tmf_parse_error<bss_sid::Individual>(R"({"givenName": 7})"), 400);
    expect_tmf_error(tmf_parse_error<bss_sid::Organization>(R"({"name": 7})"), 400);
}

// --- Project-internal Account/Subscriber API ---

TEST(SubscriberManagementInternalError, StoreRejectionIs400ProblemDetailsWithoutCause) {
    const auto handler = subscriber_management::guarded_internal(
        throwing<subscriber_management::InvalidRequest>("chargingMode is required"));
    const json body = expect_problem_without_cause(
        handler(post("/bss-api/subscriberManagement/v1/subscriber", "{}")), 400, "Bad Request");
    EXPECT_EQ(body.at("detail"), "chargingMode is required");
}

TEST(SubscriberManagementInternalError, StoreConflictIs409ProblemDetailsWithoutCause) {
    const auto handler = subscriber_management::guarded_internal(
        throwing<subscriber_management::Conflict>("an identifier is already in use"));
    const json body = expect_problem_without_cause(
        handler(post("/bss-api/subscriberManagement/v1/subscriber", "{}")), 409, "Conflict");
    EXPECT_EQ(body.at("detail"), "an identifier is already in use");
}

TEST(SubscriberManagementInternalError, UnknownResourceIs404ProblemDetailsWithoutCause) {
    const json body = expect_problem_without_cause(
        subscriber_management::internal_not_found("No Account 42"), 404, "Not Found");
    EXPECT_EQ(body.at("detail"), "No Account 42");
}

TEST(SubscriberManagementInternalError, MalformedBodyIs400ProblemDetailsWithoutCauseOrEcho) {
    const auto err = internal_parse_error<subscriber_management::Subscriber>(
        R"({"supi": "imsi-SECRET-INPUT-TOKEN",)");
    const json body = expect_problem_without_cause(err, 400, "Malformed JSON");
    EXPECT_EQ(body.at("detail"), "request body is not valid JSON");
    EXPECT_EQ(err.body.find("SECRET-INPUT-TOKEN"), std::string::npos);
}

TEST(SubscriberManagementInternalError, MissingOrWrongTypedMemberIs400ProblemDetailsWithoutCause) {
    // Account.accountKind and Subscriber.supi are read with at(): absent -> out_of_range.
    expect_problem_without_cause(internal_parse_error<subscriber_management::Account>("{}"),
                                 400,
                                 "Missing or invalid mandatory IE");
    expect_problem_without_cause(
        internal_parse_error<subscriber_management::Subscriber>(R"({"supi": 7})"),
        400,
        "Missing or invalid mandatory IE");
}

TEST(SubscriberManagementInternalError, WellFormedSubscriberBodyParses) {
    sbi_core::http2::Response err;
    const auto parsed =
        subscriber_management::parse_internal_json_body<subscriber_management::Subscriber>(
            post("/bss-api/subscriberManagement/v1/subscriber",
                 R"({"supi": "imsi-001010000000001", "chargingMode": "PREPAID"})"),
            err);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->supi, "imsi-001010000000001");
    EXPECT_EQ(parsed->chargingMode, "PREPAID");
    EXPECT_TRUE(err.body.empty());
}
