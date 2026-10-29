#pragma once

// TS 33.501 13.1.2 per-PLMN trust anchors for N32 (ADR-0529, REVIEW-5ae0ef1 SEC-01). "The SEPP
// shall maintain a set of trust anchors ... each trust anchor consists of a list of trusted root
// certificates and a list of corresponding PLMN-IDs ... Any given PLMN-ID ... shall appear in at
// most one trust anchor. During N32-c connection setup, the SEPP shall map the PLMN-ID of the
// remote SEPP end entity certificate to the associated trust anchor ... Only the root certificates
// in the associated list shall be treated as trusted ... If the remote SEPP certificate contains
// multiple PLMN-IDs that are mapped to different trust anchors, then that certificate shall be
// rejected."
//
// The PLMN-IDs of a certificate are read from its dNSName subjectAltNames under the Home Network
// Domain "5gc.mnc<MNC>.mcc<MCC>.3gppnetwork.org" (TS 23.003 28.2). TS 33.310 6.1.3c.5.3.1 leaves
// the inter-PLMN SAN contents to GSMA FS.34, which this project does not have (README "3GPP
// specification gaps").

#include <nlohmann/json.hpp>
#include <openssl/types.h>

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace sepp {

class TrustAnchors {
public:
    // `anchors`: [{"roots": ["<PEM path>", ...], "plmns": [{"mcc": "001", "mnc": "02"}, ...]},
    // ...]; relative paths are under `certs_dir`. Throws std::invalid_argument on a
    // missing/unreadable root, an empty anchor or a PLMN-ID in two anchors (TS 33.501 13.1.2).
    TrustAnchors(const nlohmann::json& anchors, const std::string& certs_dir);
    ~TrustAnchors();
    TrustAnchors(const TrustAnchors&) = delete;
    TrustAnchors& operator=(const TrustAnchors&) = delete;

    // "mcc-mnc" with a 3-digit MNC, e.g. "001-002".
    static std::set<std::string> plmns_of(X509* certificate);

    // The verdict for the remote SEPP's end-entity certificate and the chain OpenSSL built: its
    // PLMN-IDs map to exactly one anchor and the chain's root is one of that anchor's roots.
    bool accepts(X509* leaf, X509* root) const;

    // TlsConfig::verify for the N32 server and client: OpenSSL's verdict for every certificate,
    // plus accepts() for the end-entity certificate.
    bool verify(bool preverified, X509_STORE_CTX* ctx) const;

    // Every anchor's roots in one PEM file, the CA set OpenSSL builds chains against (the
    // per-anchor restriction is accepts()). Written once, at construction.
    const std::string& bundle_path() const { return bundle_path_; }

private:
    struct X509Free {
        void operator()(X509* x) const;
    };
    struct Anchor {
        std::vector<std::unique_ptr<X509, X509Free>> roots;
        std::set<std::string> plmns;
    };
    std::vector<Anchor> anchors_;
    std::string bundle_path_;
};

} // namespace sepp
