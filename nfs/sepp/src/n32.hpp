#pragma once

#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "TS26510_CommonData_grp.hpp"

// The SEPP's N32 logic that does not touch a socket (ADR-0491): PLMN domains (TS 23.003 28.2 /
// 28.5), the responding SEPP's Security Capability Negotiation decision (TS 29.573 5.2.2), and the
// correlation of an incoming N32-f request to its N32-c context when TLS is the negotiated
// security (TS 29.573 5.3.3.2). Private to nfs/sepp.

namespace sepp {

// TS 23.003 28.2: "5gc.mnc<MNC>.mcc<MCC>.3gppnetwork.org", MNC padded to three digits.
std::string home_network_domain(const std::string& mcc, const std::string& mnc);

// The host part of an apiRoot / URI / authority ("https://h:443/x" -> "h"), lower-cased.
std::string host_of(std::string_view uri_or_authority);

// The Home Network Domain a host ends with (TS 23.003 28.5.1: "the FQDN of the target NF shall
// have the Home Network Domain ... as the trailing part"), nullopt if it has none.
std::optional<std::string> network_domain_of_host(std::string_view host);

// TS 29.573 5.3.3.3: n32HandshakeId = 16HEXDIG.
bool is_handshake_id(std::string_view s);

// A partner PLMN's SEPP, from config.
struct PeerConfig {
    std::string fqdn; // the peer SEPP's FQDN (SecNegotiateReqData.sender)
    std::string mcc;  // the peer's PLMN
    std::string mnc;
    std::string n32_api_root; // where its N32-c / N32-f server is reached
    bool initiate = false;    // this SEPP starts the handshake towards it
};

// One N32-c context with TLS security negotiated.
struct Context {
    std::string peer_fqdn;
    std::string peer_domain; // the peer PLMN's Home Network Domain
    std::string n32_api_root;
    std::optional<std::string> handshake_id;
    bool peer_target_api_root = false; // peer indicated 3gpp-Sbi-Target-apiRoot support
    std::vector<std::string> purposes;
};

class ContextStore {
public:
    void put(Context c);
    bool erase(const std::string& peer_fqdn);
    std::optional<Context> by_peer(const std::string& peer_fqdn) const;
    std::optional<Context> by_domain(const std::string& domain) const;
    std::vector<Context> all() const;
    // 5.2.2 collision handling: a request this SEPP has sent and not yet had answered.
    void set_outgoing(const std::string& peer_fqdn, bool in_progress);
    bool outgoing(const std::string& peer_fqdn) const;

private:
    mutable std::mutex mutex_;
    std::vector<Context> contexts_;
    std::set<std::string> outgoing_;
};

struct NegotiationPolicy {
    std::string own_fqdn;
    std::string own_mcc;
    std::string own_mnc;
    std::vector<std::string> allowed_purposes;
    std::vector<PeerConfig> peers;
};

// What the responding SEPP answers to an exchange-capability request (TS 29.573 5.2.2 step 2a/2b,
// errors per table 6.1.6.3-1 and TS 29.500 table 5.2.7.2-1).
struct Decision {
    int status = 200;
    std::optional<sbi_gen::SecNegotiateRspData> response;
    std::optional<std::string> cause;
    std::string detail;
    std::optional<Context> context; // to store (TLS selected)
    bool teardown = false;          // NONE: delete the context for this peer
};

Decision negotiate(const NegotiationPolicy& policy,
                   const sbi_gen::SecNegotiateReqData& request,
                   bool outgoing_in_progress);

// The request this SEPP sends as initiating SEPP (supportedSecCapabilityList: TLS, or NONE to tear
// down).
sbi_gen::SecNegotiateReqData make_request(const NegotiationPolicy& policy,
                                          const PeerConfig& peer,
                                          const std::optional<std::string>& handshake_id,
                                          bool teardown);

// The context the initiating SEPP keeps from a 200 response; nullopt if the response did not select
// TLS (this SEPP offers nothing else).
std::optional<Context> context_from_response(const PeerConfig& peer,
                                             const sbi_gen::SecNegotiateRspData& response,
                                             const std::optional<std::string>& handshake_id);

// TS 29.573 5.3.3.2.1: the N32-c context an incoming N32-f request belongs to.
// - One context: the peer FQDN must appear in the client certificate's dNSName SANs; if the
//   certificate names several SEPPs (a common certificate), the Via header's "SEPP-<fqdn>" entry
//   must also name the context's peer (the clause's NOTE).
// - Several contexts: by the 3gpp-Sbi-N32-Handshake-Id header when ids were exchanged, else by
//   3gpp-Sbi-Interplmn-Purpose ("ROAMING" when absent) against each context's purposes.
std::optional<Context> correlate(const std::vector<Context>& contexts,
                                 const std::vector<std::string>& cert_dns_names,
                                 const std::string& via,
                                 const std::optional<std::string>& handshake_id,
                                 const std::optional<std::string>& purpose);

} // namespace sepp
