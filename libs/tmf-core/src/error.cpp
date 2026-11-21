#include "tmf_core/error.hpp"

#include <utility>

#include "TMF640_ServiceActivation_v4_0_0.hpp"

// See include/tmf_core/error.hpp for the source of the Error shape (TMF640 swagger, ADR-0612).

namespace tmf_core {

std::optional<std::string_view> reason_phrase(int status) {
    switch (status) {
        // RFC 9110 section 15.5 (Client Error 4xx).
        case 400:
            return "Bad Request";
        case 401:
            return "Unauthorized";
        case 402:
            return "Payment Required";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 406:
            return "Not Acceptable";
        case 407:
            return "Proxy Authentication Required";
        case 408:
            return "Request Timeout";
        case 409:
            return "Conflict";
        case 410:
            return "Gone";
        case 411:
            return "Length Required";
        case 412:
            return "Precondition Failed";
        case 413:
            return "Content Too Large";
        case 414:
            return "URI Too Long";
        case 415:
            return "Unsupported Media Type";
        case 416:
            return "Range Not Satisfiable";
        case 417:
            return "Expectation Failed";
        case 421:
            return "Misdirected Request";
        case 422:
            return "Unprocessable Content";
        case 426:
            return "Upgrade Required";
        // RFC 6585 section 4.
        case 429:
            return "Too Many Requests";
        // RFC 9110 section 15.6 (Server Error 5xx).
        case 500:
            return "Internal Server Error";
        case 501:
            return "Not Implemented";
        case 502:
            return "Bad Gateway";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        case 505:
            return "HTTP Version Not Supported";
        default:
            return std::nullopt;
    }
}

sbi_core::http2::Response error_response(int status, std::string message) {
    const std::string digits = std::to_string(status);
    tmf_gen::Error e;
    e.code = digits;
    e.reason = std::string(reason_phrase(status).value_or(digits));
    e.message = std::move(message);
    e.status = digits;

    sbi_core::http2::Response r;
    r.status = status;
    r.headers.emplace("content-type", std::string(kErrorContentType));
    // replace: a message with invalid UTF-8 must not turn an error response into an exception.
    r.body = nlohmann::json(e).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    return r;
}

} // namespace tmf_core
