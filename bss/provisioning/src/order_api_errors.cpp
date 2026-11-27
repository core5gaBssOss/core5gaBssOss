#include "order_api_errors.hpp"

#include "sbi_core/json_body.hpp"

#include <optional>

// ADR-0615: every response here is sbi_core's ProblemDetails with no cause (ADR-0612 Q5).

namespace provisioning::order_api {

sbi_core::http2::Response malformed_body() {
    return sbi_core::http2::problem_response(
        400, "Malformed JSON", "request body is not valid JSON", std::nullopt);
}

sbi_core::http2::Response order_rejected(const std::string& detail) {
    return sbi_core::http2::problem_response(400, "Bad Request", detail, std::nullopt);
}

sbi_core::http2::Response order_not_found() {
    return sbi_core::http2::problem_response(404, "Not Found", "order not found", std::nullopt);
}

} // namespace provisioning::order_api
