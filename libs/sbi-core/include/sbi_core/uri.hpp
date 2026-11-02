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

inline std::string encode_path_segment(std::string_view value) {
    static constexpr std::string_view kKeep = "-._~!$&'()*+,;=:@";
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        const bool alnum =
            (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (alnum || kKeep.find(ch) != std::string_view::npos) {
            out += ch;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

} // namespace sbi_core::uri
