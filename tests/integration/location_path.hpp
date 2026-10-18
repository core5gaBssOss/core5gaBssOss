#pragma once

// REVIEW-5ae0ef1 SBI-05: Location is an absolute "{apiRoot}/<path>" (TS 29.501 4.4.1), made so by
// sbi-core for every NF. Tests that address a created resource on a base URL they already hold, or
// compare its path, take the path part with this; the absolute form itself is asserted in
// test_sbi_robustness.cpp.

#include <string>

namespace nf_test {

// "https://127.0.0.1:7781/nudr-dr/v2/x" -> "/nudr-dr/v2/x"; a bare path is returned unchanged.
inline std::string location_path(const std::string& location) {
    const auto scheme = location.find("://");
    if (scheme == std::string::npos) {
        return location;
    }
    const auto path = location.find('/', scheme + 3);
    return path == std::string::npos ? std::string("/") : location.substr(path);
}

} // namespace nf_test
