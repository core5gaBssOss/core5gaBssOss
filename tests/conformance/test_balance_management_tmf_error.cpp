// Conformance tests for bss/balance-management's TMF654 error responses (ADR-0618, BSS SBI-03
// PR 4): the code its routes run (bss/balance-management/src/balance_errors.hpp and
// tmf_core::parse_json_body<bss_sid::TopupBalance/AdjustBalance/ReserveBalance>) answers the TM
// Forum `Error`, never 3GPP ProblemDetails.
//
// The Error definition is read at test time from the vendored TMF640 swagger
// (specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json:3405-3448), not from TMF654's:
// TMF654-PrepayBalance-v4.0.0.swagger.json is vendored by ADR-0611 on a branch this one is not cut
// from. The two `Error` definitions are identical (compared as parsed JSON, ADR-0618).

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

#include "../../bss/balance-management/src/balance_errors.hpp"
#include "../../bss/balance-management/src/store.hpp"
#include "bss_sid/balance.hpp"
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

sbi_core::http2::Request post(const std::string& resource, std::string body) {
    sbi_core::http2::Request req;
    req.method = "POST";
    req.path = "/tmf-api/prepayBalanceManagement/v4/" + resource;
    req.body = std::move(body);
    return req;
}

template <typename T> sbi_core::http2::Response parse_failure(const sbi_core::http2::Request& req) {
    sbi_core::http2::Response err;
    EXPECT_FALSE(tmf_core::parse_json_body<T>(req, err).has_value());
    return err;
}

} // namespace

TEST(BalanceManagementTmfError, StoreRejectionIs400TmfErrorWithStoreMessage) {
    const auto handler = balance_management::guarded(
        [](const sbi_core::http2::Request&) -> sbi_core::http2::Response {
            throw balance_management::InvalidRequest(
                "partyAccount acc-1 does not exist (subscriber_mgmt.account)");
        });
    const json body = expect_tmf_error(handler(post("topupBalance", "{}")), 400);
    EXPECT_EQ(body.at("reason"), "Bad Request");
    EXPECT_EQ(body.at("message"), "partyAccount acc-1 does not exist (subscriber_mgmt.account)");
}

TEST(BalanceManagementTmfError, GuardedPassesASuccessfulResponseThrough) {
    const auto handler = balance_management::guarded(
        [](const sbi_core::http2::Request&) { return sbi_core::http2::Response::json(200, "[]"); });
    const auto r = handler(post("topupBalance", "{}"));
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.body, "[]");
}

TEST(BalanceManagementTmfError, GuardedLeavesOtherExceptionsToTheFramework) {
    // Anything but InvalidRequest is not a client error: it propagates to sbi-core's 500 (PR 7).
    const auto handler = balance_management::guarded(
        [](const sbi_core::http2::Request&) -> sbi_core::http2::Response {
            throw std::runtime_error("database down");
        });
    EXPECT_THROW((void)handler(post("topupBalance", "{}")), std::runtime_error);
}

TEST(BalanceManagementTmfError, UnknownResourceIs404TmfError) {
    for (const char* resource : {"Bucket", "TopupBalance", "AdjustBalance", "ReserveBalance"}) {
        const json body = expect_tmf_error(balance_management::not_found(resource, "42"), 404);
        EXPECT_EQ(body.at("reason"), "Not Found");
        EXPECT_EQ(body.at("message"), std::string("No ") + resource + " 42");
        EXPECT_EQ(body.size(), 4u);
    }
}

TEST(BalanceManagementTmfError, MissingBucketIdIs400TmfError) {
    const json body = expect_tmf_error(balance_management::bucket_id_required(), 400);
    EXPECT_EQ(body.at("reason"), "Bad Request");
    EXPECT_EQ(body.at("message"), "bucket.id is required");
}

TEST(BalanceManagementTmfError, MissingPartyAccountFilterIs400TmfError) {
    const json body = expect_tmf_error(balance_management::party_account_id_required(), 400);
    EXPECT_EQ(body.at("reason"), "Bad Request");
    EXPECT_EQ(body.at("message"), "partyAccount.id query parameter is required");
}

TEST(BalanceManagementTmfError, MalformedBodiesAre400TmfErrorWithoutEchoingInput) {
    const std::string malformed = R"({"description": "SECRET-INPUT-TOKEN",)";
    for (const auto& err :
         {parse_failure<bss_sid::TopupBalance>(post("topupBalance", malformed)),
          parse_failure<bss_sid::AdjustBalance>(post("adjustBalance", malformed)),
          parse_failure<bss_sid::ReserveBalance>(post("reserveBalance", malformed))}) {
        expect_tmf_error(err, 400);
        EXPECT_EQ(err.body.find("SECRET-INPUT-TOKEN"), std::string::npos);
    }
}

TEST(BalanceManagementTmfError, BucketWithoutIdIs400TmfError) {
    // BucketRef reads `id` with at(): an id-less bucket fails in the parse, before the route's own
    // bucket.id check.
    const auto err =
        parse_failure<bss_sid::AdjustBalance>(post("adjustBalance", R"({"bucket":{}})"));
    expect_tmf_error(err, 400);
}

TEST(BalanceManagementTmfError, WrongTypedMemberIs400TmfError) {
    const auto err =
        parse_failure<bss_sid::ReserveBalance>(post("reserveBalance", R"({"bucket":{"id":7}})"));
    expect_tmf_error(err, 400);
}

TEST(BalanceManagementTmfError, WellFormedTopupBodyParses) {
    sbi_core::http2::Response err;
    const auto parsed = tmf_core::parse_json_body<bss_sid::TopupBalance>(
        post("topupBalance", R"({"bucket": {"id": "b-1"}, "description": "x"})"), err);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->bucket.has_value());
    EXPECT_EQ(parsed->bucket->id, "b-1");
    EXPECT_TRUE(err.body.empty());
}
