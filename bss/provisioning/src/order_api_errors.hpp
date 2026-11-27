#pragma once

// Error responses of the project-internal order API, /provisioning/v1/customerOrder (ADR-0615,
// BSS SBI-03 PR 2). That API is not a TM Forum API and not a 3GPP SBI API, so (ADR-0612 Q5) it
// keeps a ProblemDetails body {status, title, detail} but never carries a TS 29.500 `cause`: the
// 3GPP application error vocabulary belongs to 3GPP SBI APIs only.

#include "sbi_core/http2_server.hpp"

#include <string>

namespace provisioning::order_api {

// POST body that is not JSON. Fixed detail: the parse error's text can quote the input, and the
// body carries SIM keys.
sbi_core::http2::Response malformed_body();

// POST order the store rejected; `detail` is ProvisionResult::error (never carries K/OPc).
sbi_core::http2::Response order_rejected(const std::string& detail);

// GET /provisioning/v1/customerOrder/{id} for an unknown id.
sbi_core::http2::Response order_not_found();

} // namespace provisioning::order_api
