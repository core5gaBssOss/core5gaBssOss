#pragma once

// The TMF620 Product Catalog Management error responses of bss/product-catalog (ADR-0619, BSS
// SBI-03 PR 5). Header-only and outside main() so tests/conformance can drive the same code the
// routes run.
//
// Every 4xx/5xx response of every TMF620 operation this service serves has the schema
// `#/definitions/Error` (specs/tmforum/TMF620-ProductCatalog-v4.1.0.swagger.json:9165-9208,
// vendored by ADR-0611), the same definition as TMF640's, which libs/tmf-core builds (ADR-0612).
// POST declares 400 and 409 (no 404); GET {id} and DELETE {id} declare 400, 404 and 409.

#include "sbi_core/http2_server.hpp"

#include <string>
#include <string_view>
#include <utility>

#include "store.hpp"
#include "tmf_core/error.hpp"

namespace product_catalog {

// ADR-0384: the normalized store rejects what the relational model cannot accept. InvalidRequest
// is a 400 and Conflict a 409 TMF Error, each with the store's own text as message, instead of a
// 500. Any other exception is left to the sbi-core framework.
template <typename Handler> auto guarded(Handler handler) {
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

// GET or DELETE /{entity}/{id} for an id that names no such entity, e.g.
// not_found("ProductOffering", "42") -> 404 "No ProductOffering 42".
inline sbi_core::http2::Response not_found(std::string_view entity, const std::string& id) {
    return tmf_core::error_response(404, "No " + std::string(entity) + " " + id);
}

} // namespace product_catalog
