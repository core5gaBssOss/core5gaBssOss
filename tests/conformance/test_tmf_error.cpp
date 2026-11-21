// Conformance tests for libs/tmf-core's TM Forum Error helper (ADR-0612, BSS SBI-03 PR 1).
// The Error definition is read at test time from the vendored swagger
// (specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json:3405-3448, `definitions.Error`), and
// the content type from the same file's top-level `produces` (lines 16-18), so a body member or
// content type that the swagger does not declare fails here rather than in review.

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

#include "TMF640_ServiceActivation_v4_0_0.hpp"
#include "tmf_core/error.hpp"

#include <gtest/gtest.h>

using nlohmann::json;

namespace {

const json& tmf640_swagger() {
    static const json doc = [] {
        std::ifstream in(TMF640_SWAGGER_PATH);
        return json::parse(in);
    }();
    return doc;
}

const json& error_definition() {
    return tmf640_swagger().at("definitions").at("Error");
}

std::string content_type(const sbi_core::http2::Response& r) {
    const auto it = r.headers.find("content-type");
    return it == r.headers.end() ? std::string{} : it->second;
}

// Every member is a declared property of the TMF640 Error, every declared-string one is a string,
// every required one is present, and no 3GPP ProblemDetails member (cause, title, detail) leaks in.
void expect_tmf640_error(const sbi_core::http2::Response& r, int status) {
    EXPECT_EQ(r.status, status);
    ASSERT_EQ(r.headers.count("content-type"), 1u);
    EXPECT_EQ(content_type(r), "application/json;charset=utf-8");

    const json body = json::parse(r.body);
    ASSERT_TRUE(body.is_object());
    const json& def = error_definition();
    const json& props = def.at("properties");
    for (const auto& required : def.at("required")) {
        EXPECT_TRUE(body.contains(required.get<std::string>())) << required;
    }
    for (const auto& [name, value] : body.items()) {
        ASSERT_TRUE(props.contains(name)) << "member not declared by TMF640 Error: " << name;
        if (props.at(name).value("type", "") == "string") {
            EXPECT_TRUE(value.is_string()) << name;
        }
    }
    EXPECT_FALSE(body.contains("cause"));
    EXPECT_FALSE(body.contains("title"));
    EXPECT_FALSE(body.contains("detail"));

    EXPECT_EQ(body.at("code"), std::to_string(status));
    EXPECT_EQ(body.at("status"), std::to_string(status));
    // The helper's output also reads back through the generated DTO.
    EXPECT_NO_THROW((void)body.get<tmf_gen::Error>());
}

sbi_core::http2::Request request_with_body(std::string body) {
    sbi_core::http2::Request req;
    req.method = "POST";
    req.body = std::move(body);
    return req;
}

} // namespace

TEST(TmfError, SwaggerDeclaresTheShapeThisHelperRelies) {
    // Guards the citation in tmf_core/error.hpp against a re-vendored swagger.
    const json& def = error_definition();
    EXPECT_EQ(def.at("required"), json::array({"code", "reason"}));
    for (const char* name : {"code", "reason", "message", "status"}) {
        EXPECT_EQ(def.at("properties").at(name).at("type"), "string") << name;
    }
    EXPECT_EQ(tmf640_swagger().at("produces"),
              json::array({std::string(tmf_core::kErrorContentType)}));
}

TEST(TmfError, NotFoundCarriesCodeReasonMessageAndStringStatus) {
    const auto r = tmf_core::error_response(404, "service svc-9 not found");
    expect_tmf640_error(r, 404);
    const json body = json::parse(r.body);
    EXPECT_EQ(body.at("reason"), "Not Found");
    EXPECT_EQ(body.at("message"), "service svc-9 not found");
    EXPECT_TRUE(body.at("status").is_string());
    EXPECT_FALSE(body.contains("referenceError"));
    EXPECT_FALSE(body.contains("@type"));
    EXPECT_EQ(body.size(), 4u);
}

TEST(TmfError, EveryStatusTheBssUsesHasItsReasonPhrase) {
    const std::pair<int, const char*> cases[] = {{400, "Bad Request"},
                                                 {404, "Not Found"},
                                                 {409, "Conflict"},
                                                 {500, "Internal Server Error"},
                                                 {502, "Bad Gateway"}};
    for (const auto& [status, phrase] : cases) {
        const auto r = tmf_core::error_response(status, "x");
        expect_tmf640_error(r, status);
        EXPECT_EQ(json::parse(r.body).at("reason"), phrase);
    }
}

TEST(TmfError, StatusOutsideThePhraseTableStillFillsRequiredReason) {
    EXPECT_FALSE(tmf_core::reason_phrase(499).has_value());
    const auto r = tmf_core::error_response(499, "x");
    expect_tmf640_error(r, 499);
    EXPECT_EQ(json::parse(r.body).at("reason"), "499");
}

TEST(TmfError, InvalidUtf8MessageDoesNotThrow) {
    const auto r = tmf_core::error_response(400, std::string("bad \xff byte"));
    expect_tmf640_error(r, 400);
}

TEST(TmfError, ParseJsonBodyMalformedJsonIs400TmfErrorWithoutEchoingInput) {
    const auto req = request_with_body(R"({"code": "SECRET-INPUT-TOKEN",)");
    sbi_core::http2::Response err;
    EXPECT_FALSE(tmf_core::parse_json_body<tmf_gen::Error>(req, err).has_value());
    expect_tmf640_error(err, 400);
    EXPECT_EQ(err.body.find("SECRET-INPUT-TOKEN"), std::string::npos);
}

TEST(TmfError, ParseJsonBodyMissingMandatoryMemberIs400TmfError) {
    // tmf_gen::Error requires `reason` (swagger Error.required).
    const auto req = request_with_body(R"({"code": "SECRET-INPUT-TOKEN"})");
    sbi_core::http2::Response err;
    EXPECT_FALSE(tmf_core::parse_json_body<tmf_gen::Error>(req, err).has_value());
    expect_tmf640_error(err, 400);
    EXPECT_EQ(err.body.find("SECRET-INPUT-TOKEN"), std::string::npos);
}

TEST(TmfError, ParseJsonBodyWrongTypeIs400TmfError) {
    const auto req = request_with_body(R"({"code": 7, "reason": "SECRET-INPUT-TOKEN"})");
    sbi_core::http2::Response err;
    EXPECT_FALSE(tmf_core::parse_json_body<tmf_gen::Error>(req, err).has_value());
    expect_tmf640_error(err, 400);
    EXPECT_EQ(err.body.find("SECRET-INPUT-TOKEN"), std::string::npos);
}

TEST(TmfError, ParseJsonBodyValidBodyLeavesErrorUntouched) {
    const auto req = request_with_body(R"({"code": "404", "reason": "Not Found"})");
    sbi_core::http2::Response err;
    const auto parsed = tmf_core::parse_json_body<tmf_gen::Error>(req, err);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->code, "404");
    EXPECT_EQ(parsed->reason, "Not Found");
    EXPECT_TRUE(err.body.empty());
}
