#pragma once

#include "sbi_core/http2_server.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

// Shared TM Forum Open API error helper for the BSS TM Forum servers (ADR-0612, BSS SBI-03 PR 1).
//
// Source: the TM Forum `Error` definition in
// specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json:3405-3448 (TM Forum Open API TMF640
// Service Activation v4.0.0, tmforum-apis/TMF640_ActivationConfiguration, commit
// 44d0e9e81f3d27239fa31e4c86b568dd22eeacf1 -- specs/tmforum/SOURCES.md): required [code, reason];
// properties code, reason, message, status (all string), referenceError (uri), @baseType,
// @schemaLocation, @type. The body is the generated tmf_gen::Error (libs/tmf-generated), never a
// hand-written DTO. The content type is the swagger's `produces`, application/json;charset=utf-8
// (same file, lines 16-18).
//
// Based on TMF640 only. That TMF620/632/651/654 declare the identical Error definition is confirmed
// by the vendoring of those four swagger files (ADR-0611), not by this file.
//
// Member values (user decisions 2026-11-21, ADR-0612): TMF640 defines no `code` list, so `code` is
// the HTTP status as a string (what bss/provisioning's TMF640 handlers already do); `reason` is the
// HTTP reason phrase; `message` is the caller's detail text; `status` is the HTTP status as a
// string; referenceError, @baseType, @schemaLocation and @type are left absent. No 3GPP cause is
// ever sent: these are TM Forum APIs, not 3GPP SBI APIs.

namespace tmf_core {

// The content type of every response this helper builds.
inline constexpr std::string_view kErrorContentType = "application/json;charset=utf-8";

// The reason phrase RFC 9110 section 15.5/15.6 gives `status` (429: RFC 6585 section 4), e.g.
// "Not Found" for 404. std::nullopt for a status outside that table.
std::optional<std::string_view> reason_phrase(int status);

// A response with the given HTTP status and a TMF Error body {code, reason, message, status}.
// `reason` is reason_phrase(status); for a status outside that table it falls back to the status
// digits, so the required member is never empty and no phrase is made up.
sbi_core::http2::Response error_response(int status, std::string message);

// Parses req.body as JSON into T (a generated tmf_gen DTO). On success returns the value; on
// failure writes a 400 TMF Error into err_out and returns nullopt. Mirrors
// sbi_core::http2::parse_json_body (libs/sbi-core/include/sbi_core/json_body.hpp), but the error
// carries no 3GPP cause, and its message is fixed text: nlohmann's own what() for a parse error
// quotes the offending input ("last read: '...'"), which is never echoed back.
template <typename T>
std::optional<T> parse_json_body(const sbi_core::http2::Request& req,
                                 sbi_core::http2::Response& err_out) {
    try {
        return nlohmann::json::parse(req.body).get<T>();
    } catch (const nlohmann::json::parse_error&) {
        err_out = error_response(400, "The request body is not well-formed JSON");
    } catch (const nlohmann::json::out_of_range&) {
        err_out = error_response(400, "The request body lacks a mandatory member");
    } catch (const nlohmann::json::exception&) {
        err_out = error_response(400, "A member of the request body has an invalid type or value");
    }
    return std::nullopt;
}

} // namespace tmf_core
