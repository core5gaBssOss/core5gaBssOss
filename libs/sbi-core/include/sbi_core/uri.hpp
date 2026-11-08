#pragma once

#include <string>
#include <string_view>

// Building a request URI from a value that is not ours (a SUPI, an id another NF chose).
// TS 29.501 5.2.3 and RFC 3986 3.3: a resource id goes into one path segment, so every character
// outside RFC 3986 "pchar" (unreserved / sub-delims / ":" / "@") is percent-encoded. That covers
// '/', '?', '#', '%', spaces and control bytes, so the value can neither add a segment nor end
// the path. An "imsi-..." or "nai-user@realm" SUPI is unchanged. The sbi_core server decodes a
// path parameter only after splitting the path (http2_server.cpp), so the peer gets the value
// back byte for byte.

namespace sbi_core::uri {

namespace detail {

inline std::string percent_encode_except(std::string_view value, std::string_view keep) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        const bool alnum =
            (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (alnum || keep.find(ch) != std::string_view::npos) {
            out += ch;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

} // namespace detail

inline std::string encode_path_segment(std::string_view value) {
    return detail::percent_encode_except(value, "-._~!$&'()*+,;=:@");
}

// A query parameter value (RFC 3986 3.4, form style): also encodes '&', '=', '+', ';' and '#',
// so the value can neither start another parameter nor end the query.
inline std::string encode_query_value(std::string_view value) {
    return detail::percent_encode_except(value, "-._~!$'()*,:@/?");
}

} // namespace sbi_core::uri
