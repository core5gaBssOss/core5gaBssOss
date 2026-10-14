#include "n32.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace sepp {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::ranges::transform(
        out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

const PeerConfig* find_peer(const NegotiationPolicy& policy, const std::string& fqdn) {
    const auto it = std::ranges::find_if(
        policy.peers, [&](const PeerConfig& p) { return lower(p.fqdn) == lower(fqdn); });
    return it == policy.peers.end() ? nullptr : &*it;
}

Decision reject(int status, std::string cause, std::string detail) {
    Decision d;
    d.status = status;
    d.cause = std::move(cause);
    d.detail = std::move(detail);
    return d;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::ranges::find(v, s) != v.end();
}

// The FQDNs of the "SEPP-<fqdn>" entries of a Via header (TS 29.500 5.2.2.2 / 6.10.10.3: e.g.
// "2.0 SEPP-sepp1.operator.com"), lower-cased.
std::vector<std::string> via_sepps(const std::string& via) {
    std::vector<std::string> out;
    static const std::regex kEntry(R"(SEPP-([A-Za-z0-9.\-]+))");
    for (auto it = std::sregex_iterator(via.begin(), via.end(), kEntry);
         it != std::sregex_iterator();
         ++it) {
        out.push_back(lower((*it)[1].str()));
    }
    return out;
}

} // namespace

std::string home_network_domain(const std::string& mcc, const std::string& mnc) {
    return "5gc.mnc" + (mnc.size() == 2 ? "0" + mnc : mnc) + ".mcc" + mcc + ".3gppnetwork.org";
}

std::string host_of(std::string_view uri_or_authority) {
    std::string_view s = uri_or_authority;
    if (const auto scheme = s.find("://"); scheme != std::string_view::npos) {
        s.remove_prefix(scheme + 3);
    }
    s = s.substr(0, s.find('/'));
    if (const auto at = s.rfind('@'); at != std::string_view::npos) {
        s.remove_prefix(at + 1);
    }
    if (!s.empty() && s.front() == '[') { // IPv6 literal
        return lower(s.substr(0, s.find(']') + 1));
    }
    return lower(s.substr(0, s.find(':')));
}

std::optional<std::string> network_domain_of_host(std::string_view host) {
    static const std::regex kDomain(R"((^|\.)(5gc\.mnc[0-9]{3}\.mcc[0-9]{3}\.3gppnetwork\.org)$)");
    const std::string h = lower(host);
    std::smatch m;
    if (std::regex_search(h, m, kDomain)) {
        return m[2].str();
    }
    return std::nullopt;
}

bool is_handshake_id(std::string_view s) {
    return s.size() == 16 &&
           std::ranges::all_of(s, [](unsigned char c) { return std::isxdigit(c); });
}

void ContextStore::put(Context c) {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(contexts_, [&](const Context& x) { return x.peer_fqdn == c.peer_fqdn; });
    contexts_.push_back(std::move(c));
}

bool ContextStore::erase(const std::string& peer_fqdn) {
    const std::lock_guard<std::mutex> lock(mutex_);
    return std::erase_if(contexts_, [&](const Context& x) { return x.peer_fqdn == peer_fqdn; }) > 0;
}

std::optional<Context> ContextStore::by_peer(const std::string& peer_fqdn) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it =
        std::ranges::find_if(contexts_, [&](const Context& x) { return x.peer_fqdn == peer_fqdn; });
    return it == contexts_.end() ? std::nullopt : std::optional{*it};
}

std::optional<Context> ContextStore::by_domain(const std::string& domain) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it =
        std::ranges::find_if(contexts_, [&](const Context& x) { return x.peer_domain == domain; });
    return it == contexts_.end() ? std::nullopt : std::optional{*it};
}

std::vector<Context> ContextStore::all() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return contexts_;
}

void ContextStore::set_outgoing(const std::string& peer_fqdn, bool in_progress) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (in_progress) {
        outgoing_.insert(peer_fqdn);
    } else {
        outgoing_.erase(peer_fqdn);
    }
}

bool ContextStore::outgoing(const std::string& peer_fqdn) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return outgoing_.contains(peer_fqdn);
}

Decision negotiate(const NegotiationPolicy& policy,
                   const sbi_gen::SecNegotiateReqData& request,
                   bool outgoing_in_progress) {
    const PeerConfig* peer = find_peer(policy, request.sender);
    if (peer == nullptr) {
        // Not a configured roaming / interconnect partner: no security capability is configured
        // for it at this SEPP (table 6.1.6.3-1).
        return reject(403,
                      "NEGOTIATION_NOT_ALLOWED",
                      "no security capability is configured for SEPP " + request.sender);
    }
    if (request.targetPlmnId &&
        home_network_domain(request.targetPlmnId->mcc, request.targetPlmnId->mnc) !=
            home_network_domain(policy.own_mcc, policy.own_mnc)) {
        return reject(400, "OPTIONAL_IE_INCORRECT", "targetPlmnId is not a PLMN this SEPP serves");
    }
    if (request.n32HandshakeId && !is_handshake_id(*request.n32HandshakeId)) {
        return reject(400, "OPTIONAL_IE_INCORRECT", "n32HandshakeId must be 16 hexadecimal digits");
    }

    sbi_gen::SecNegotiateRspData rsp;
    rsp.sender = policy.own_fqdn;
    rsp.plmnIdList = std::vector<sbi_gen::PlmnId_CommonData>{{policy.own_mcc, policy.own_mnc}};

    const auto& caps = request.supportedSecCapabilityList;
    const auto has = [&](const std::string& c) {
        return std::ranges::any_of(caps, [&](const auto& x) { return x.value == c; });
    };
    // Tear-down of the N32-f TLS connection: the capability set to "NONE" (5.2.2 step 1 / 2a).
    if (has(sbi_gen::SecurityCapability::NONE)) {
        Decision d;
        rsp.selectedSecCapability.value = sbi_gen::SecurityCapability::NONE;
        d.response = rsp;
        d.teardown = true;
        return d;
    }
    // Collision: both SEPPs initiated; the one whose FQDN lexicographically precedes continues.
    if (outgoing_in_progress && lower(policy.own_fqdn) < lower(request.sender)) {
        return reject(409,
                      "N32C_EXCHANGE_CAPABILITY_ONGOING",
                      "this SEPP's own exchange-capability request to " + request.sender +
                          " is in progress and takes precedence");
    }
    if (!has(sbi_gen::SecurityCapability::TLS)) {
        // PRINS is not supported by this SEPP (ADR-0491 increment 1).
        return reject(403, "NEGOTIATION_NOT_ALLOWED", "this SEPP supports TLS security only");
    }

    // Purposes: none exchanged means roaming and inter-PLMN mobility (5.2.2, 6.1.5.3.9).
    std::vector<std::string> requested;
    if (request.intendedUsagePurpose) {
        for (const auto& p : *request.intendedUsagePurpose) {
            requested.push_back(p.usagePurpose.value);
        }
    } else {
        requested = {sbi_gen::N32Purpose::ROAMING, sbi_gen::N32Purpose::INTER_PLMN_MOBILITY};
    }
    std::vector<std::string> allowed;
    std::vector<sbi_gen::IntendedN32Purpose> allowed_ie;
    std::vector<sbi_gen::IntendedN32Purpose> rejected_ie;
    for (const auto& p : requested) {
        sbi_gen::IntendedN32Purpose ie;
        ie.usagePurpose.value = p;
        if (contains(policy.allowed_purposes, p)) {
            allowed.push_back(p);
            allowed_ie.push_back(ie);
        } else {
            rejected_ie.push_back(ie);
        }
    }
    if (allowed.empty()) {
        return reject(403,
                      "REQUESTED_PURPOSE_NOT_ALLOWED",
                      "none of the requested N32 purposes is allowed with this SEPP");
    }
    if (request.intendedUsagePurpose) {
        rsp.allowedUsagePurpose = allowed_ie;
        if (!rejected_ie.empty()) {
            rsp.rejectedUsagePurpose = rejected_ie;
        }
    }

    rsp.selectedSecCapability.value = sbi_gen::SecurityCapability::TLS;
    rsp.n3GppSbiTargetApiRootSupported = true;
    rsp.n32HandshakeId = request.n32HandshakeId;

    Context ctx;
    ctx.peer_fqdn = lower(peer->fqdn);
    ctx.peer_domain = home_network_domain(peer->mcc, peer->mnc);
    ctx.n32_api_root = peer->n32_api_root;
    ctx.handshake_id = request.n32HandshakeId;
    ctx.peer_target_api_root = request.n3GppSbiTargetApiRootSupported.value_or(false);
    ctx.purposes = allowed;

    Decision d;
    d.response = rsp;
    d.context = ctx;
    return d;
}

sbi_gen::SecNegotiateReqData make_request(const NegotiationPolicy& policy,
                                          const PeerConfig& peer,
                                          const std::optional<std::string>& handshake_id,
                                          bool teardown) {
    sbi_gen::SecNegotiateReqData req;
    req.sender = policy.own_fqdn;
    sbi_gen::SecurityCapability cap;
    cap.value = teardown ? sbi_gen::SecurityCapability::NONE : sbi_gen::SecurityCapability::TLS;
    req.supportedSecCapabilityList = {cap};
    if (!teardown) {
        req.n3GppSbiTargetApiRootSupported = true;
        req.plmnIdList = std::vector<sbi_gen::PlmnId_CommonData>{{policy.own_mcc, policy.own_mnc}};
        req.targetPlmnId = sbi_gen::PlmnId_CommonData{peer.mcc, peer.mnc};
        std::vector<sbi_gen::IntendedN32Purpose> purposes;
        for (const auto& p : policy.allowed_purposes) {
            sbi_gen::IntendedN32Purpose ie;
            ie.usagePurpose.value = p;
            purposes.push_back(ie);
        }
        if (!purposes.empty()) {
            req.intendedUsagePurpose = purposes;
        }
    }
    req.n32HandshakeId = handshake_id;
    return req;
}

std::optional<Context> context_from_response(const PeerConfig& peer,
                                             const sbi_gen::SecNegotiateRspData& response,
                                             const std::optional<std::string>& handshake_id) {
    if (response.selectedSecCapability.value != sbi_gen::SecurityCapability::TLS) {
        return std::nullopt;
    }
    Context ctx;
    ctx.peer_fqdn = lower(peer.fqdn);
    ctx.peer_domain = home_network_domain(peer.mcc, peer.mnc);
    ctx.n32_api_root = peer.n32_api_root;
    // The id is shared only if the responder echoed it (it supports the N32 handshake id).
    if (handshake_id && response.n32HandshakeId == handshake_id) {
        ctx.handshake_id = handshake_id;
    }
    ctx.peer_target_api_root = response.n3GppSbiTargetApiRootSupported.value_or(false);
    if (response.allowedUsagePurpose) {
        for (const auto& p : *response.allowedUsagePurpose) {
            ctx.purposes.push_back(p.usagePurpose.value);
        }
    } else {
        ctx.purposes = {sbi_gen::N32Purpose::ROAMING, sbi_gen::N32Purpose::INTER_PLMN_MOBILITY};
    }
    return ctx;
}

std::optional<Context> correlate(const std::vector<Context>& contexts,
                                 const std::vector<std::string>& cert_dns_names,
                                 const std::string& via,
                                 const std::optional<std::string>& handshake_id,
                                 const std::optional<std::string>& purpose) {
    if (contexts.empty()) {
        return std::nullopt;
    }
    if (contexts.size() == 1) {
        const auto& ctx = contexts.front();
        std::vector<std::string> names;
        for (const auto& n : cert_dns_names) {
            names.push_back(lower(n));
        }
        if (!contains(names, ctx.peer_fqdn)) {
            return std::nullopt;
        }
        // A certificate naming several SEPPs (one common certificate): the Via header decides.
        const auto sepp_names = std::ranges::count_if(
            names, [](const std::string& n) { return network_domain_of_host(n).has_value(); });
        if (sepp_names > 1 && !contains(via_sepps(via), ctx.peer_fqdn)) {
            return std::nullopt;
        }
        return ctx;
    }
    const bool ids_exchanged =
        std::ranges::all_of(contexts, [](const Context& c) { return c.handshake_id.has_value(); });
    if (ids_exchanged) {
        if (!handshake_id) {
            return std::nullopt;
        }
        const auto it = std::ranges::find_if(contexts, [&](const Context& c) {
            return lower(*c.handshake_id) == lower(*handshake_id);
        });
        return it == contexts.end() ? std::nullopt : std::optional{*it};
    }
    const std::string p = purpose.value_or(sbi_gen::N32Purpose::ROAMING);
    std::optional<Context> match;
    for (const auto& c : contexts) {
        if (contains(c.purposes, p)) {
            if (match) {
                return std::nullopt; // ambiguous
            }
            match = c;
        }
    }
    return match;
}

} // namespace sepp
