#pragma once

#include "sbi_core/http2_server.hpp"
#include "sbi_core/multipart.hpp"
#include "sbi_core/problem_details.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string_view>
#include <tl/expected.hpp>

// Shared request-body helpers for NF request handlers: build a ProblemDetails error response, and
// parse a JSON (or multipart/related-wrapped JSON) request body into a generated sbi_gen DTO.
//
// Promoted here from nfs/nrf and nfs/amf, which each independently wrote (and, for nrf, still
// have) their own copy of this exact pattern -- nfs/amf's parse_multipart_json_body made it a
// third occurrence (SMF, this file's first consumer), past the point where duplicating it again
// was the right call. nfs/nrf's and nfs/amf's own local copies are left as-is (already tested,
// already committed) rather than churned to use this in the same turn that introduces it -- a
// disclosed, deliberate, non-blocking cleanup opportunity for later, not silently left unnoticed.
// See docs/DECISIONS.md for the ADR this supports.

namespace sbi_core::http2 {

// `cause` is required (REVIEW-5ae0ef1 SBI-03): a TS 29.500 Table 5.2.7.2-1 or API-specific
// application error, or std::nullopt where neither defines one for this error.
Response problem_response(int status,
                          const std::string& title,
                          const std::string& detail,
                          std::optional<std::string> cause);

// The TS 29.500 Table 5.2.7.2-1 cause for a failed read of a JSON body into a DTO: a member that
// is absent (nlohmann out_of_range) is MANDATORY_IE_MISSING, one of the wrong type or value
// MANDATORY_IE_INCORRECT.
const char* dto_error_cause(const nlohmann::json::exception& e);

// A PATCH body as every R19 YAML this repo serves declares it for "application/json-patch+json":
// a JSON Patch (RFC 6902) document, an array of PatchItem with minItems 1 -- applied to `target`.
// Returns the patched document, or the response to send instead (REVIEW-5ae0ef1 SPEC-02/04):
// - another content type: 415 with Accept-Patch: application/json-patch+json (TS 29.500 5.2.7.2);
// - malformed JSON, a repeated member name (TS 33.117, ADR-0354), not an array, an empty array,
//   or a patch that cannot be applied (bad path, failed "test"): 400 INVALID_MSG_FORMAT.
// The caller still validates the result against the resource's schema.
tl::expected<nlohmann::json, Response> apply_json_patch_body(const Request& req,
                                                             const nlohmann::json& target);

// True when `body` is well-formed JSON in which some object repeats a member name. nlohmann::json
// (like most parsers) keeps the last value silently, so a request such as
// {"supi":"imsi-1","supi":"imsi-2"} would be read as the second while a validating front end may
// have read the first -- the ambiguity TS 33.117 (as recorded in ADR-0354) requires an
// implementation to reject. Malformed JSON returns false: the handler's own parse reports it.
bool json_has_duplicate_keys(std::string_view body);

// Parses req.body as JSON into T (a generated sbi_gen DTO). On success returns the value; on
// failure writes a 400 ProblemDetails into err_out and returns nullopt.
template <typename T> std::optional<T> parse_json_body(const Request& req, Response& err_out) {
    try {
        return nlohmann::json::parse(req.body).get<T>();
    } catch (const nlohmann::json::parse_error& e) {
        err_out = problem_response(400, "Malformed JSON", e.what(), "INVALID_MSG_FORMAT");
    } catch (const nlohmann::json::exception& e) {
        err_out =
            problem_response(400, "Missing or invalid mandatory IE", e.what(), dto_error_cause(e));
    }
    return std::nullopt;
}

// Same contract, for operations whose request body is multipart/related (RFC 2046/2387 -- see
// multipart.hpp): extracts and parses the root (jsonData) part. Binary parts (N1/N2/GTP-C content)
// are validated as present in the wire format by sbi_core::multipart::parse but not otherwise
// interpreted here -- callers that need them can re-parse req.body themselves via
// sbi_core::multipart::parse for the non-root parts.
template <typename T>
std::optional<T> parse_multipart_json_body(const Request& req, Response& err_out) {
    const auto content_type_it = req.headers.find("content-type");
    if (content_type_it == req.headers.end() ||
        !sbi_core::multipart::is_multipart_related(content_type_it->second)) {
        err_out = problem_response(400,
                                   "Unsupported Media Type",
                                   "This operation requires a multipart/related body",
                                   "INVALID_MSG_FORMAT");
        return std::nullopt;
    }
    auto parts = sbi_core::multipart::parse(content_type_it->second, req.body);
    if (!parts.has_value()) {
        err_out =
            problem_response(400, "Malformed multipart body", parts.error(), "INVALID_MSG_FORMAT");
        return std::nullopt;
    }
    if (parts->empty() || (*parts)[0].content_type.find("application/json") == std::string::npos) {
        err_out = problem_response(400,
                                   "Malformed multipart body",
                                   "first part (jsonData) must be application/json",
                                   "INVALID_MSG_FORMAT");
        return std::nullopt;
    }
    try {
        return nlohmann::json::parse((*parts)[0].body).get<T>();
    } catch (const nlohmann::json::parse_error& e) {
        err_out = problem_response(
            400, "Malformed JSON in jsonData part", e.what(), "INVALID_MSG_FORMAT");
    } catch (const nlohmann::json::exception& e) {
        err_out =
            problem_response(400, "Missing or invalid mandatory IE", e.what(), dto_error_cause(e));
    }
    return std::nullopt;
}

} // namespace sbi_core::http2
