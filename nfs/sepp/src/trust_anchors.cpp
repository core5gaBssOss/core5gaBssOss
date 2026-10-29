#include "trust_anchors.hpp"

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace sepp {

namespace {

std::string plmn_key(const std::string& mcc, std::string mnc) {
    while (mnc.size() < 3) {
        mnc.insert(mnc.begin(), '0');
    }
    return mcc + "-" + mnc;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::invalid_argument("sepp: trust anchor root " + path + " cannot be read");
    }
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

} // namespace

TrustAnchors::TrustAnchors(const nlohmann::json& anchors, const std::string& certs_dir) {
    if (!anchors.is_array() || anchors.empty()) {
        throw std::invalid_argument("sepp: trust_anchors must be a non-empty array");
    }
    std::set<std::string> seen;
    std::string bundle;
    for (const auto& a : anchors) {
        Anchor anchor;
        for (const auto& r : a.value("roots", nlohmann::json::array())) {
            std::filesystem::path p = r.get<std::string>();
            if (p.is_relative()) {
                p = std::filesystem::path(certs_dir) / p;
            }
            const auto pem = read_file(p.string());
            std::unique_ptr<BIO, decltype(&BIO_free)> bio(
                BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())), &BIO_free);
            X509* cert = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
            if (cert == nullptr) {
                throw std::invalid_argument("sepp: trust anchor root " + p.string() +
                                            " is not a PEM certificate");
            }
            anchor.roots.emplace_back(cert);
            bundle += pem;
            if (!bundle.empty() && bundle.back() != '\n') {
                bundle += '\n';
            }
        }
        for (const auto& plmn : a.value("plmns", nlohmann::json::array())) {
            const auto key =
                plmn_key(plmn.at("mcc").get<std::string>(), plmn.at("mnc").get<std::string>());
            if (!seen.insert(key).second) {
                throw std::invalid_argument("sepp: PLMN " + key +
                                            " appears in two trust anchors (TS 33.501 13.1.2)");
            }
            anchor.plmns.insert(key);
        }
        if (anchor.roots.empty() || anchor.plmns.empty()) {
            throw std::invalid_argument("sepp: a trust anchor needs roots and plmns");
        }
        anchors_.push_back(std::move(anchor));
    }
    // The union of roots for OpenSSL's own chain building.
    std::string tmpl =
        (std::filesystem::temp_directory_path() / "sepp-n32-roots-XXXXXX.pem").string();
    const int fd = mkstemps(tmpl.data(), 4);
    if (fd < 0) {
        throw std::runtime_error("sepp: cannot create the N32 root bundle");
    }
    const auto written = write(fd, bundle.data(), bundle.size());
    close(fd);
    if (written != static_cast<ssize_t>(bundle.size())) {
        throw std::runtime_error("sepp: cannot write the N32 root bundle");
    }
    bundle_path_ = tmpl;
}

void TrustAnchors::X509Free::operator()(X509* x) const {
    X509_free(x);
}

TrustAnchors::~TrustAnchors() {
    if (!bundle_path_.empty()) {
        std::error_code ec;
        std::filesystem::remove(bundle_path_, ec);
    }
}

std::set<std::string> TrustAnchors::plmns_of(X509* certificate) {
    std::set<std::string> out;
    // TS 23.003 28.2 Home Network Domain, as the trailing labels of a dNSName (a wildcard or a
    // SEPP-id label may precede it).
    static const std::regex kDomain(R"((^|\.)5gc\.mnc([0-9]{3})\.mcc([0-9]{3})\.3gppnetwork\.org$)",
                                    std::regex::icase);
    auto* names = static_cast<GENERAL_NAMES*>(
        X509_get_ext_d2i(certificate, NID_subject_alt_name, nullptr, nullptr));
    if (names == nullptr) {
        return out;
    }
    for (int i = 0; i < sk_GENERAL_NAME_num(names); ++i) {
        const GENERAL_NAME* gn = sk_GENERAL_NAME_value(names, i);
        if (gn->type != GEN_DNS) {
            continue;
        }
        const auto* data = ASN1_STRING_get0_data(gn->d.dNSName);
        const std::string dns(reinterpret_cast<const char*>(data),
                              static_cast<std::size_t>(ASN1_STRING_length(gn->d.dNSName)));
        std::smatch m;
        if (std::regex_search(dns, m, kDomain)) {
            out.insert(m[3].str() + "-" + m[2].str());
        }
    }
    GENERAL_NAMES_free(names);
    return out;
}

bool TrustAnchors::accepts(X509* leaf, X509* root) const {
    const auto plmns = plmns_of(leaf);
    if (plmns.empty()) {
        return false; // no PLMN-ID to map to a trust anchor
    }
    const Anchor* match = nullptr;
    for (const auto& plmn : plmns) {
        const Anchor* found = nullptr;
        for (const auto& a : anchors_) {
            if (a.plmns.contains(plmn)) {
                found = &a;
            }
        }
        if (found == nullptr || (match != nullptr && match != found)) {
            return false; // an unknown PLMN-ID, or PLMN-IDs of different anchors
        }
        match = found;
    }
    for (const auto& r : match->roots) {
        if (X509_cmp(r.get(), root) == 0) {
            return true;
        }
    }
    return false; // chained to a root of another anchor
}

bool TrustAnchors::verify(bool preverified, X509_STORE_CTX* ctx) const {
    if (!preverified) {
        return false;
    }
    if (X509_STORE_CTX_get_error_depth(ctx) != 0) {
        return true; // the anchor decision is made once, for the end-entity certificate
    }
    STACK_OF(X509)* chain = X509_STORE_CTX_get0_chain(ctx);
    if (chain == nullptr || sk_X509_num(chain) < 1) {
        return false;
    }
    X509* leaf = sk_X509_value(chain, 0);
    X509* root = sk_X509_value(chain, sk_X509_num(chain) - 1);
    return accepts(leaf, root);
}

} // namespace sepp
