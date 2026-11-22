#pragma once

// The TMF654 Prepay Balance Management error responses of bss/balance-management (ADR-0618, BSS
// SBI-03 PR 4). Header-only and outside main() so tests/conformance can drive the same code the
// routes run. Named balance_errors.hpp, not tmf_errors.hpp, because conformance_tests also has
// bss/roaming-interconnect/src on its include path (ADR-0616's tmf_errors.hpp).
//
// Every 4xx/5xx response of every TMF654 operation has the schema `#/definitions/Error`
// (TMF654-PrepayBalance-v4.0.0.swagger.json:5972-6015, vendored by ADR-0611), the same definition
// as TMF640's, which libs/tmf-core builds (ADR-0612). The three POST operations declare 400 (no
// 404); every GET operation declares 400 and 404.

#include "sbi_core/http2_server.hpp"

#include <string>
#include <utility>

#include "store.hpp"
#include "tmf_core/error.hpp"

namespace balance_management {

// ADR-0385: store rejections (InvalidRequest: a malformed date-time, an unknown partyAccount) are
// client errors, not 500s. They answer a 400 TMF Error whose message is the store's own text.
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

// GET /{resource}/{id} for an id that names no such resource.
inline sbi_core::http2::Response not_found(const std::string& resource, const std::string& id) {
    return tmf_core::error_response(404, "No " + resource + " " + id);
}

// POST /topupBalance, /adjustBalance or /reserveBalance whose body has no bucket.id.
inline sbi_core::http2::Response bucket_id_required() {
    return tmf_core::error_response(400, "bucket.id is required");
}

// GET /accumulatedBalance without the partyAccount.id filter (src/main.cpp header discloses that
// this parameter is not itemized in the swagger's own parameter list for the operation).
inline sbi_core::http2::Response party_account_id_required() {
    return tmf_core::error_response(400, "partyAccount.id query parameter is required");
}

} // namespace balance_management
