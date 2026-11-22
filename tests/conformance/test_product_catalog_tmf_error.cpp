// Conformance tests for bss/product-catalog's TMF620 error responses (ADR-0619, BSS SBI-03 PR 5):
// the code its routes run (bss/product-catalog/src/tmf_errors.hpp and tmf_core::parse_json_body
// on the three bss_sid DTOs) answers the TM Forum `Error`, never 3GPP ProblemDetails.
//
// The Error definition is read at test time from the vendored TMF640 swagger
// (specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json:3405-3448), not from TMF620's:
// TMF620-ProductCatalog-v4.1.0.swagger.json is vendored by ADR-0611 on a branch this one is not cut
// from. The two `Error` definitions are identical (ADR-0619 compared them as parsed JSON).

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <string>

#include "bss_sid/product.hpp"
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

sbi_core::http2::Request post(std::string path, std::string body) {
    sbi_core::http2::Request req;
    req.method = "POST";
    req.path = "/tmf-api/productCatalogManagement/v4/" + std::move(path);
    req.body = std::move(body);
    return req;
}

template <typename T> sbi_core::http2::Response parse_failure(const sbi_core::http2::Request& req) {
    sbi_core::http2::Response err;
    EXPECT_FALSE(tmf_core::parse_json_body<T>(req, err).has_value());
    return err;
}

} // namespace

TEST(ProductCatalogTmfError, StoreInvalidRequestIs400TmfErrorWithStoreMessage) {
    const auto handler =
        product_catalog::guarded([](const sbi_core::http2::Request&) -> sbi_core::http2::Response {
            throw product_catalog::InvalidRequest("a mandatory field is missing");
        });
    const json body = expect_tmf_error(handler(post("productOffering", "{}")), 400);
    EXPECT_EQ(body.at("reason"), "Bad Request");
    EXPECT_EQ(body.at("message"), "a mandatory field is missing");
    EXPECT_EQ(body.size(), 4u);
}

TEST(ProductCatalogTmfError, StoreConflictIs409TmfErrorWithStoreMessage) {
    const auto handler =
        product_catalog::guarded([](const sbi_core::http2::Request&) -> sbi_core::http2::Response {
            throw product_catalog::Conflict(
                "ProductOfferingPrice 7 is still referenced and cannot be deleted");
        });
    const json body = expect_tmf_error(handler(post("productOfferingPrice", "{}")), 409);
    EXPECT_EQ(body.at("reason"), "Conflict");
    EXPECT_EQ(body.at("message"),
              "ProductOfferingPrice 7 is still referenced and cannot be deleted");
    EXPECT_EQ(body.size(), 4u);
}

TEST(ProductCatalogTmfError, GuardedPassesASuccessfulResponseThrough) {
    const auto handler = product_catalog::guarded(
        [](const sbi_core::http2::Request&) { return sbi_core::http2::Response::json(200, "[]"); });
    const auto r = handler(post("productOffering", "{}"));
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.body, "[]");
}

TEST(ProductCatalogTmfError, GuardedLeavesOtherExceptionsToTheFramework) {
    // Anything but InvalidRequest/Conflict is not a client error: it propagates to sbi-core's 500
    // (BSS SBI-03 PR 7).
    const auto handler =
        product_catalog::guarded([](const sbi_core::http2::Request&) -> sbi_core::http2::Response {
            throw std::runtime_error("database down");
        });
    EXPECT_THROW((void)handler(post("productOffering", "{}")), std::runtime_error);
}

TEST(ProductCatalogTmfError, UnknownEntityIs404TmfErrorForEachResource) {
    for (const char* entity : {"ProductOffering", "ProductOfferingPrice", "ProductSpecification"}) {
        const json body = expect_tmf_error(product_catalog::not_found(entity, "42"), 404);
        EXPECT_EQ(body.at("reason"), "Not Found");
        EXPECT_EQ(body.at("message"), std::string("No ") + entity + " 42");
        EXPECT_EQ(body.size(), 4u);
    }
}

TEST(ProductCatalogTmfError, MalformedBodyIs400TmfErrorWithoutEchoingInputForEachResource) {
    const std::string malformed = R"({"name": "SECRET-INPUT-TOKEN",)";
    for (const auto& err :
         {parse_failure<bss_sid::ProductOffering>(post("productOffering", malformed)),
          parse_failure<bss_sid::ProductOfferingPrice>(post("productOfferingPrice", malformed)),
          parse_failure<bss_sid::ProductSpecification>(post("productSpecification", malformed))}) {
        expect_tmf_error(err, 400);
        EXPECT_EQ(err.body.find("SECRET-INPUT-TOKEN"), std::string::npos);
    }
}

TEST(ProductCatalogTmfError, WrongTypedMemberIs400TmfErrorForEachResource) {
    // The three bss_sid DTOs read `name` as a string.
    const std::string wrong = R"({"name": 7})";
    expect_tmf_error(parse_failure<bss_sid::ProductOffering>(post("productOffering", wrong)), 400);
    expect_tmf_error(
        parse_failure<bss_sid::ProductOfferingPrice>(post("productOfferingPrice", wrong)), 400);
    expect_tmf_error(
        parse_failure<bss_sid::ProductSpecification>(post("productSpecification", wrong)), 400);
}

TEST(ProductCatalogTmfError, WellFormedOfferingBodyParses) {
    sbi_core::http2::Response err;
    const auto parsed = tmf_core::parse_json_body<bss_sid::ProductOffering>(
        post("productOffering", R"({"name": "5G Unlimited", "isSellable": true})"), err);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->name, "5G Unlimited");
    EXPECT_TRUE(err.body.empty());
}
