#pragma once

// The TMF651 Agreement Management error responses of bss/roaming-interconnect (ADR-0616, BSS
// SBI-03 PR 3). Header-only and outside main() so tests/conformance can drive the same code the
// routes run.
//
// Every 4xx/5xx response of every TMF651 operation has the schema `#/definitions/Error`
// (specs/tmforum/TMF651-Agreement-v4.0.0.swagger.json:3351-3394, vendored by ADR-0611), the same
// definition as TMF640's, which libs/tmf-core builds (ADR-0612). POST /agreement declares 400
// (no 404); GET /agreement/{id} declares 400 and 404.

#include "sbi_core/http2_server.hpp"

#include <string>
#include <utility>

#include "store.hpp"
#include "tmf_core/error.hpp"

namespace roaming_interconnect {

// ADR-0387: relational-model rejections (InvalidRequest from the store) are client errors, not
// 500s. They answer a 400 TMF Error whose message is the store's own text.
template <typename Handler> auto guarded(Handler handler) {
    return [handler = std::move(handler)](
               const sbi_core::http2::Request& req) -> sbi_core::http2::Response {
        try {
            return handler(req);
        } catch (const InvalidRequest& e) {
            return tmf_core::error_response(400, e.what());
        }
    };
}

// GET /agreement/{id} for an id that names no InterconnectAgreement.
inline sbi_core::http2::Response agreement_not_found(const std::string& id) {
    return tmf_core::error_response(404, "No InterconnectAgreement " + id);
}

} // namespace roaming_interconnect
