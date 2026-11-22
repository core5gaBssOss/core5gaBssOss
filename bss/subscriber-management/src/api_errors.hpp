#pragma once

// The error responses of bss/subscriber-management (ADR-0620, BSS SBI-03 PR 6). Header-only and
// outside main() so tests/conformance can drive the same code the routes run.
//
// This service serves two APIs, and each gets its own error shape:
//
// - TMF632 Party Management v4 (`/tmf-api/party/v4`, /individual and /organization). Every 4xx/5xx
//   response of every TMF632 operation has the schema `#/definitions/Error`
//   (specs/tmforum/TMF632-Party-v4.0.0.swagger.json:3919-3962, vendored by ADR-0611), the same
//   definition as TMF640's, which libs/tmf-core builds (ADR-0612). POST /individual and POST
//   /organization declare 400 and 409 (no 404); GET .../{id} declares 400, 404 and 409.
// - The project-internal Account/Subscriber API (`/bss-api/subscriberManagement/v1`). Not a TM
//   Forum API and not a 3GPP SBI API, so (ADR-0612 Q5) it keeps ProblemDetails {status, title,
//   detail} but never carries a TS 29.500 `cause`.

#include "sbi_core/http2_server.hpp"
#include "sbi_core/json_body.hpp"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>

#include "store.hpp"
#include "tmf_core/error.hpp"

namespace subscriber_management {

// --- TMF632 Party Management ---

// ADR-0386: relational-model rejections are client errors, not 500s. On the TMF632 routes they
// answer the TMF Error, with the store's own text as `message`: InvalidRequest -> 400,
// Conflict -> 409.
template <typename Handler> auto guarded_tmf(Handler handler) {
    return [handler = std::move(handler)](
               const sbi_core::http2::Request& req) -> sbi_core::http2::Response {
        try {
            return handler(req);
        } catch (const InvalidRequest& e) {
            return tmf_core::error_response(400, e.what());
        } catch (const Conflict& e) {
            return tmf_core::error_response(409, e.what());
        }
    };
}

// GET /individual/{id} or /organization/{id} for an id that names no such party. `kind` is
// "Individual" or "Organization".
inline sbi_core::http2::Response party_not_found(const std::string& kind, const std::string& id) {
    return tmf_core::error_response(404, "No " + kind + " " + id);
}

// --- Project-internal Account/Subscriber API ---

// The same ADR-0386 mapping on the internal routes, as ProblemDetails without a cause.
template <typename Handler> auto guarded_internal(Handler handler) {
    return [handler = std::move(handler)](
               const sbi_core::http2::Request& req) -> sbi_core::http2::Response {
        try {
            return handler(req);
        } catch (const InvalidRequest& e) {
            return sbi_core::http2::problem_response(400, "Bad Request", e.what(), std::nullopt);
        } catch (const Conflict& e) {
            return sbi_core::http2::problem_response(409, "Conflict", e.what(), std::nullopt);
        }
    };
}

// GET on an internal resource that does not exist; `detail` names it (e.g. "No Account 42").
inline sbi_core::http2::Response internal_not_found(const std::string& detail) {
    return sbi_core::http2::problem_response(404, "Not Found", detail, std::nullopt);
}

// Parses req.body into T (Account or Subscriber). Same titles as sbi_core::http2::parse_json_body,
// but no TS 29.500 cause (ADR-0612 Q5), and a parse error's detail is fixed text: nlohmann's own
// what() for it quotes the offending input ("last read: '...'"), which is never echoed back. The
// what() of a missing or wrongly typed member names the member and the expected type, not the
// value, and is kept as the detail.
template <typename T>
std::optional<T> parse_internal_json_body(const sbi_core::http2::Request& req,
                                          sbi_core::http2::Response& err_out) {
    try {
        return nlohmann::json::parse(req.body).get<T>();
    } catch (const nlohmann::json::parse_error&) {
        err_out = sbi_core::http2::problem_response(
            400, "Malformed JSON", "request body is not valid JSON", std::nullopt);
    } catch (const nlohmann::json::exception& e) {
        err_out = sbi_core::http2::problem_response(
            400, "Missing or invalid mandatory IE", e.what(), std::nullopt);
    }
    return std::nullopt;
}

} // namespace subscriber_management
