// ADR-0529 (REVIEW-5ae0ef1 SEC-01): TS 33.501 13.1.2 per-PLMN trust anchors. Two independent CAs
// and SEPP end-entity certificates are made in-process with OpenSSL, so each case is a real chain.

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "trust_anchors.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;
using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using Cert = std::unique_ptr<X509, decltype(&X509_free)>;

Key new_key() {
    return Key(EVP_EC_gen("P-256"), &EVP_PKEY_free);
}

// A certificate for `cn`, signed by `issuer_key` (self-signed when `issuer` is null), with the
// given dNSName SANs; a CA when `ca` is set.
Cert make_cert(const std::string& cn,
               EVP_PKEY* key,
               X509* issuer,
               EVP_PKEY* issuer_key,
               const std::vector<std::string>& dns,
               bool ca) {
    Cert c(X509_new(), &X509_free);
    X509_set_version(c.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(c.get()),
                     static_cast<long>(std::hash<std::string>{}(cn) & 0x7fffffff));
    X509_gmtime_adj(X509_getm_notBefore(c.get()), -60);
    X509_gmtime_adj(X509_getm_notAfter(c.get()), 3600);
    X509_set_pubkey(c.get(), key);
    X509_NAME* name = X509_get_subject_name(c.get());
    X509_NAME_add_entry_by_txt(
        name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(cn.c_str()), -1, -1, 0);
    X509_set_issuer_name(c.get(), issuer != nullptr ? X509_get_subject_name(issuer) : name);
    X509V3_CTX v3;
    X509V3_set_ctx(&v3, issuer != nullptr ? issuer : c.get(), c.get(), nullptr, nullptr, 0);
    if (ca) {
        X509_EXTENSION* bc =
            X509V3_EXT_conf_nid(nullptr, &v3, NID_basic_constraints, "critical,CA:TRUE");
        X509_add_ext(c.get(), bc, -1);
        X509_EXTENSION_free(bc);
    }
    if (!dns.empty()) {
        std::string san;
        for (const auto& d : dns) {
            san += (san.empty() ? "DNS:" : ",DNS:") + d;
        }
        X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &v3, NID_subject_alt_name, san.c_str());
        X509_add_ext(c.get(), ext, -1);
        X509_EXTENSION_free(ext);
    }
    X509_sign(c.get(), issuer_key, EVP_sha256());
    return c;
}

std::string write_pem(const std::filesystem::path& dir, const std::string& name, X509* c) {
    const auto p = dir / name;
    FILE* f = std::fopen(p.c_str(), "w");
    PEM_write_X509(f, c);
    std::fclose(f);
    return p.string();
}

struct Pki {
    std::filesystem::path dir = std::filesystem::path(::testing::TempDir()) / "sepp_anchors";
    Key ca1_key = new_key(), ca2_key = new_key(), leaf_key = new_key();
    Cert ca1 = make_cert("CA-1", ca1_key.get(), nullptr, ca1_key.get(), {}, true);
    Cert ca2 = make_cert("CA-2", ca2_key.get(), nullptr, ca2_key.get(), {}, true);
    Pki() {
        std::filesystem::create_directories(dir);
        write_pem(dir, "ca1.pem", ca1.get());
        write_pem(dir, "ca2.pem", ca2.get());
    }
    // Anchor 1: CA-1 for PLMN 001/02; anchor 2: CA-2 for PLMN 001/03.
    json anchors() const {
        return json::array({json{{"roots", json::array({"ca1.pem"})},
                                 {"plmns", json::array({json{{"mcc", "001"}, {"mnc", "02"}}})}},
                            json{{"roots", json::array({"ca2.pem"})},
                                 {"plmns", json::array({json{{"mcc", "001"}, {"mnc", "03"}}})}}});
    }
    Cert leaf(X509* ca, EVP_PKEY* ca_key, const std::vector<std::string>& dns) {
        return make_cert("sepp", leaf_key.get(), ca, ca_key, dns, false);
    }
};

TEST(SeppTrustAnchors, PlmnIdsComeFromTheHomeNetworkDomainSans) {
    Pki pki;
    const auto c = pki.leaf(pki.ca1.get(),
                            pki.ca1_key.get(),
                            {"sepp.5gc.mnc002.mcc001.3gppnetwork.org",
                             "*.sepp.5gc.mnc002.mcc001.3gppnetwork.org",
                             "localhost"});
    EXPECT_EQ(sepp::TrustAnchors::plmns_of(c.get()), (std::set<std::string>{"001-002"}));
}

TEST(SeppTrustAnchors, OnlyTheRootsOfThePlmnsOwnAnchorAreTrusted) {
    Pki pki;
    const sepp::TrustAnchors anchors(pki.anchors(), pki.dir.string());
    const auto good =
        pki.leaf(pki.ca1.get(), pki.ca1_key.get(), {"sepp.5gc.mnc002.mcc001.3gppnetwork.org"});
    EXPECT_TRUE(anchors.accepts(good.get(), pki.ca1.get()));

    // PLMN 001/02 chained to CA-2, the other anchor's root: rejected (13.1.2 "Only the root
    // certificates in the associated list shall be treated as trusted").
    const auto wrong_root =
        pki.leaf(pki.ca2.get(), pki.ca2_key.get(), {"sepp.5gc.mnc002.mcc001.3gppnetwork.org"});
    EXPECT_FALSE(anchors.accepts(wrong_root.get(), pki.ca2.get()));

    // PLMN-IDs of two different anchors in one certificate: rejected (13.1.2).
    const auto two = pki.leaf(
        pki.ca1.get(),
        pki.ca1_key.get(),
        {"sepp.5gc.mnc002.mcc001.3gppnetwork.org", "sepp.5gc.mnc003.mcc001.3gppnetwork.org"});
    EXPECT_FALSE(anchors.accepts(two.get(), pki.ca1.get()));

    // No PLMN-ID, or a PLMN-ID in no anchor: nothing to map, rejected.
    const auto none = pki.leaf(pki.ca1.get(), pki.ca1_key.get(), {"sepp", "localhost"});
    EXPECT_FALSE(anchors.accepts(none.get(), pki.ca1.get()));
    const auto unknown =
        pki.leaf(pki.ca1.get(), pki.ca1_key.get(), {"sepp.5gc.mnc009.mcc001.3gppnetwork.org"});
    EXPECT_FALSE(anchors.accepts(unknown.get(), pki.ca1.get()));
}

TEST(SeppTrustAnchors, APlmnInTwoAnchorsIsAConfigurationError) {
    Pki pki;
    json bad = pki.anchors();
    bad[1]["plmns"].push_back(json{{"mcc", "001"}, {"mnc", "02"}});
    EXPECT_THROW(sepp::TrustAnchors(bad, pki.dir.string()), std::invalid_argument);
    json missing = pki.anchors();
    missing[0]["roots"] = json::array({"absent.pem"});
    EXPECT_THROW(sepp::TrustAnchors(missing, pki.dir.string()), std::invalid_argument);
}

TEST(SeppTrustAnchors, TheRootBundleHoldsEveryAnchorsRoots) {
    Pki pki;
    const sepp::TrustAnchors anchors(pki.anchors(), pki.dir.string());
    std::ifstream in(anchors.bundle_path());
    const std::string pem((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t n = 0;
    for (auto p = pem.find("BEGIN CERTIFICATE"); p != std::string::npos;
         p = pem.find("BEGIN CERTIFICATE", p + 1)) {
        ++n;
    }
    EXPECT_EQ(n, 2U);
}

} // namespace
