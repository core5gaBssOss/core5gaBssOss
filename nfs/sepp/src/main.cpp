// nfs/sepp: SEPP -- Security Edge Protection Proxy, increment 1 (ADR-0491): N32-c and N32-f with
// TLS security.
//
// Stage 3: TS 29.573 V19.7.0 (specs/3gpp/TS_29.573_j70.txt) and TS29573_N32_Handshake.yaml
// (specs/5G_APIs-REL-19, forge.3gpp.org REL-19, commit bca84b6; info.version 1.4.0, DTOs generated
// into sbi_gen). Routing: TS 29.500 V19.7.0 6.1.4.3. FQDNs: TS 23.003 28.2 / 28.5.
//
//   N32-c  {apiRoot}/n32c-handshake/v1/exchange-capability  5.2.2 Security Capability Negotiation
//                                                          (TLS; NONE tears the context down)
//          {apiRoot}/n32c-handshake/v1/n32f-terminate      5.2.4 (no n32fContextId exists under
//                                                          TLS -- always 404, see below)
//          {apiRoot}/n32c-handshake/v1/n32f-error          5.2.5 (logged, 204)
//   N32-f  any other path on the N32 port                  5.3.3 forwarding with TLS security
//   NF side any path on the SBI port                       6.1.4.3.3: an NF in this PLMN sends the
//                                                          request with 3gpp-Sbi-Target-apiRoot, or
//                                                          6.1.4.3.2: to <label>.<this SEPP's FQDN>
//           {apiRoot}/nsepp-telescopic/v1/mapping          5.4.2 / 5.4.3 (ADR-0492)
//
// Two servers: the SBI port faces the NFs of this PLMN, the N32 port faces partner SEPPs. Both are
// TLS 1.3 + mTLS (the N32-c "end to end TLS" of 5.2.1 is the N32 port's TLS).
//
// DISCLOSED, not hidden:
//   * TLS security only. PRINS (5.2.3 parameter exchange, 5.3.2 JOSE-protected forwarding) is not
//     built: an offer of PRINS alone is refused 403 NEGOTIATION_NOT_ALLOWED; exchange-params is
//     answered the same way. Hence no n32fContextId ever exists, and n32f-terminate always
//     answers 404.
//   * N32-f always carries 3gpp-Sbi-Target-apiRoot (both SEPPs indicate support -- this one
//     always does); forwarding to a peer that does not support the header (the target URI itself
//     on N32-f) is not built. Telescopic FQDNs (ADR-0492): served; only the Location header of a
//     response is rewritten into telescopic form, not URIs inside bodies (NF profiles, callbacks),
//     and an unknown label is not looked up at other SEPPs of this PLMN (single SEPP here).
//   * The receiving SEPP reaches an NF of its own PLMN through `local_nf_routes` in config (the
//     target authority -> a reachable apiRoot): it stands in for the DNS resolution of the
//     3gppnetwork.org FQDN, which this lab does not run.
//   * Roaming intermediaries (5.5), N32-f keepalive PINGs (5.3.3.5), TLSCOR OPTIONS (5.3.3.2.2),
//     SNDN32F (senderN32fFqdn / port), topology hiding and message filtering are not built.
//   * The SEPP does not validate the NFs' OAuth2 tokens (they are for the target producer).
//   * NRF: registered as nfType SEPP (fqdn, plmnList) with heartbeat; no SeppInfo (ports / N32
//     purposes) is advertised, and NFs do not discover the SEPP through the NRF here.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/io_context_pool.hpp"
#include "sbi_core/logging.hpp"
#include "sbi_core/metrics.hpp"
#include "sbi_core/oauth2_client.hpp"
#include "sbi_core/otel.hpp"
#include "sbi_core/problem_details.hpp"
#include "sbi_core/sbi_headers.hpp"
#include "sbi_core/uuid.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "TS29573_SeppTelescopicFqdnMapping.hpp"
#include "n32.hpp"
#include "nf_config/nf_config.hpp"

namespace {

using nlohmann::json;
namespace http2 = sbi_core::http2;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/sepp/CMakeLists.txt)"
#endif
#ifndef CONFIG_DIR
#error "CONFIG_DIR must be defined by CMake (see nfs/sepp/CMakeLists.txt)"
#endif

constexpr const char* kHandshakeRoot = "/n32c-handshake/v1";
constexpr const char* kTargetApiRoot = "3gpp-sbi-target-apiroot";
constexpr const char* kHandshakeIdHeader = "3gpp-sbi-n32-handshake-id";
constexpr const char* kPurposeHeader = "3gpp-sbi-interplmn-purpose";

http2::TlsConfig sepp_tls() {
    return http2::TlsConfig{
        .cert_path = CERTS_DIR "/sepp/cert.pem",
        .key_path = CERTS_DIR "/sepp/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
}

// TS 29.500 5.2.7: ProblemDetails with the application error cause; the Server header names this
// SEPP as the originator (TS 29.500 6.10.10 / table 5.2.2.2-2: "SEPP-<SEPP FQDN>").
http2::Response problem(int status,
                        const std::string& title,
                        const std::string& detail,
                        const std::optional<std::string>& cause,
                        const std::string& own_fqdn) {
    http2::Response r;
    r.status = status;
    r.headers.emplace("content-type", "application/problem+json");
    r.headers.emplace("server", "SEPP-" + own_fqdn);
    r.body = json(sbi_core::make_problem_details(status, title, detail, cause)).dump();
    return r;
}

std::optional<std::string> header(const http2::Request& req, const char* name) {
    const auto it = req.headers.find(name);
    return it == req.headers.end() ? std::nullopt : std::optional{it->second};
}

std::string random_handshake_id() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(rng()));
    return buf;
}

// Headers that describe one hop, not the message: never relayed.
bool hop_by_hop(const std::string& name) {
    return name.empty() || name.front() == ':' || name == "host" || name == "content-length" ||
           name == "connection" || name == "keep-alive" || name == "te" ||
           name == "transfer-encoding" || name == "upgrade" || name == "proxy-connection";
}

std::string lower(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

// Relays `req` to `url`, dropping `drop` headers, adding `extra`; the Via header gets this SEPP
// appended on the request and on the response (TS 29.500 6.10.10.3).
http2::Response relay(http2::Client& client,
                      const http2::Request& req,
                      const std::string& url,
                      const std::vector<std::string>& drop,
                      const std::vector<std::pair<std::string, std::string>>& extra,
                      const std::string& own_fqdn) {
    const std::string me = "2.0 SEPP-" + own_fqdn;
    http2::ClientRequest out;
    out.method = req.method;
    out.url = url;
    out.body = req.body;
    std::string via;
    for (const auto& [name, value] : req.headers) {
        const std::string n = lower(name);
        if (hop_by_hop(n) || std::ranges::find(drop, n) != drop.end()) {
            continue;
        }
        if (n == "via") {
            via = via.empty() ? value : via + ", " + value;
            continue;
        }
        out.headers.emplace(n, value);
    }
    for (const auto& [name, value] : extra) {
        out.headers.emplace(name, value);
    }
    out.headers.emplace("via", via.empty() ? me : via + ", " + me);

    auto r = client.send(out);
    if (!r) {
        return problem(504,
                       "Gateway Timeout",
                       "next hop not reachable: " + r.error(),
                       "TARGET_NF_NOT_REACHABLE",
                       own_fqdn);
    }
    http2::Response resp;
    resp.status = static_cast<int>(r->status);
    std::string rvia;
    for (const auto& [name, value] : r->headers) {
        const std::string n = lower(name);
        if (hop_by_hop(n)) {
            continue;
        }
        if (n == "via") {
            rvia = rvia.empty() ? value : rvia + ", " + value;
            continue;
        }
        resp.headers.emplace(n, value);
    }
    resp.headers.emplace("via", rvia.empty() ? me : rvia + ", " + me);
    resp.body = std::move(r->body);
    return resp;
}

// "https://h:p/prefix" -> {"h:p", "/prefix"}.
std::pair<std::string, std::string> split_api_root(const std::string& api_root) {
    std::string s = api_root;
    if (const auto scheme = s.find("://"); scheme != std::string::npos) {
        s = s.substr(scheme + 3);
    }
    const auto slash = s.find('/');
    if (slash == std::string::npos) {
        return {lower(s), ""};
    }
    std::string prefix = s.substr(slash);
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    return {lower(s.substr(0, slash)), prefix};
}

// NRF registration + heartbeat (TS 29.510 NFRegister / NFUpdate), same shape as every NF here
// (e.g. nfs/eir run_nrf_lifecycle); on its own thread, never on the server's io_context.
void run_nrf_lifecycle(const std::string& instance_id,
                       const std::string& nrf_base,
                       const std::string& fqdn,
                       const std::string& mcc,
                       const std::string& mnc,
                       const std::atomic<bool>& running) {
    http2::Client http_client(sepp_tls());
    sbi_core::OAuth2Client oauth(
        http_client, nrf_base + "/oauth2/token", instance_id, "nnrf-nfm", "NRF");
    constexpr int kHeartbeatSeconds = 30;
    const json profile{
        {"nfInstanceId", instance_id},
        {"nfType", "SEPP"},
        {"nfStatus", "REGISTERED"},
        {"fqdn", fqdn},
        {"plmnList", json::array({json{{"mcc", mcc}, {"mnc", mnc}}})},
        {"ipv4Addresses", json::array({"127.0.0.1"})},
        {"heartBeatTimer", kHeartbeatSeconds},
    };
    const auto pause = [&](int seconds) {
        for (int i = 0; i < seconds * 10 && running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return running.load();
    };
    bool registered = false;
    while (!registered && running.load()) {
        auto token = oauth.get_bearer_token();
        if (token) {
            http2::ClientRequest put;
            put.method = "PUT";
            put.url = nrf_base + "/nnrf-nfm/v1/nf-instances/" + instance_id;
            put.headers.emplace("content-type", "application/json");
            put.headers.emplace("authorization", "Bearer " + *token);
            put.headers.emplace(
                sbi_core::headers::kSenderTimestamp,
                sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
            put.body = profile.dump();
            const auto r = http_client.send(put);
            registered = r && (r->status == 200 || r->status == 201);
            if (registered) {
                spdlog::info("sepp: registered with NRF (HTTP {})", r->status);
                break;
            }
        }
        spdlog::warn("sepp: NRF registration attempt failed, retrying in 5s");
        pause(5);
    }
    while (pause(kHeartbeatSeconds / 2)) {
        auto token = oauth.get_bearer_token();
        if (!token) {
            continue;
        }
        http2::ClientRequest patch;
        patch.method = "PATCH";
        patch.url = nrf_base + "/nnrf-nfm/v1/nf-instances/" + instance_id;
        patch.headers.emplace("content-type", "application/json-patch+json");
        patch.headers.emplace("authorization", "Bearer " + *token);
        patch.body =
            json::array({json{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}}})
                .dump();
        if (const auto r = http_client.send(patch); !r || r->status != 200) {
            spdlog::warn("sepp: NRF heartbeat failed");
        }
    }
}

} // namespace

int main() {
    sbi_core::init_logging("sepp");
    sbi_core::init_tracing("sepp");

    const auto config = nf_config::load("sepp", CONFIG_DIR);
    const auto port = nf_config::require<unsigned short>(config, "port", "SEPP_PORT");
    const auto n32_port = nf_config::require<unsigned short>(config, "n32_port", "SEPP_N32_PORT");
    const auto bind = nf_config::require<std::string>(config, "sbi_bind_address");
    const auto metrics_bind_address = nf_config::require<std::string>(
        config, "metrics_bind_address", "SEPP_METRICS_BIND_ADDRESS");
    const auto retry_seconds = config.value("handshake_retry_seconds", 2);
    const auto nrf_base =
        nf_config::require<std::string>(config, "nrf_base_url", "SEPP_NRF_BASE_URL");

    sepp::NegotiationPolicy policy;
    policy.own_fqdn = lower(nf_config::require<std::string>(config, "fqdn", "SEPP_FQDN"));
    policy.own_mcc = nf_config::require<std::string>(config, "mcc", "SEPP_MCC");
    policy.own_mnc = nf_config::require<std::string>(config, "mnc", "SEPP_MNC");
    policy.allowed_purposes = config.at("allowed_purposes").get<std::vector<std::string>>();
    for (const auto& p : config.at("peers")) {
        policy.peers.push_back(sepp::PeerConfig{lower(p.at("fqdn").get<std::string>()),
                                                p.at("mcc").get<std::string>(),
                                                p.at("mnc").get<std::string>(),
                                                p.at("n32_api_root").get<std::string>(),
                                                p.value("initiate", false)});
    }
    // Target authority ("host:port", lower case) -> apiRoot this SEPP can reach (see header).
    std::map<std::string, std::string> local_nf_routes;
    if (config.contains("local_nf_routes")) {
        for (const auto& [authority, api_root] : config.at("local_nf_routes").items()) {
            local_nf_routes.emplace(lower(authority), api_root.get<std::string>());
        }
    }
    const std::string own_domain = sepp::home_network_domain(policy.own_mcc, policy.own_mnc);

    sbi_core::init_metrics(metrics_bind_address);
    auto meter = sbi_core::get_meter("sepp");
    auto handshakes = meter->CreateUInt64Counter(
        "sepp_n32c_handshakes_total", "N32-c security capability negotiations completed");
    auto forwarded = meter->CreateUInt64Counter("sepp_n32f_forwarded_total",
                                                "Requests relayed over N32-f (both directions)");
    auto refused = meter->CreateUInt64Counter("sepp_n32f_refused_total",
                                              "N32-f requests refused (no context, bad target)");

    spdlog::info(
        "sepp: {} serving PLMN {} ({} peer(s))", policy.own_fqdn, own_domain, policy.peers.size());

    sepp::ContextStore contexts;
    sepp::TelescopicTable telescopic;
    http2::Client client(sepp_tls());

    boost::asio::io_context ioc;
    http2::Server sbi(ioc, bind, port, sepp_tls());
    http2::Server n32(ioc, bind, n32_port, sepp_tls());

    // ---- N32-c (TS 29.573 6.1) ----------------------------------------------------------------
    n32.add_route(
        "POST",
        std::string(kHandshakeRoot) + "/exchange-capability",
        [&](const http2::Request& req) {
            sbi_gen::SecNegotiateReqData body;
            try {
                body = json::parse(req.body).get<sbi_gen::SecNegotiateReqData>();
            } catch (const std::exception& e) {
                return problem(400, "Bad Request", e.what(), "INVALID_MSG_FORMAT", policy.own_fqdn);
            }
            const auto d = sepp::negotiate(policy, body, contexts.outgoing(lower(body.sender)));
            if (!d.response) {
                spdlog::warn("sepp: exchange-capability from {} refused: {} ({})",
                             body.sender,
                             d.cause.value_or(""),
                             d.detail);
                return problem(d.status,
                               d.status == 409   ? "Conflict"
                               : d.status == 400 ? "Bad Request"
                                                 : "Forbidden",
                               d.detail,
                               d.cause,
                               policy.own_fqdn);
            }
            if (d.teardown) {
                contexts.erase(lower(body.sender));
                spdlog::info("sepp: N32 context with {} torn down by the peer", body.sender);
            } else if (d.context) {
                contexts.put(*d.context);
                handshakes->Add(1);
                spdlog::info("sepp: N32 context with {} (TLS, handshake id {})",
                             d.context->peer_fqdn,
                             d.context->handshake_id.value_or("-"));
            }
            return http2::Response::json(200, json(*d.response).dump());
        });
    n32.add_route(
        "POST", std::string(kHandshakeRoot) + "/exchange-params", [&](const http2::Request&) {
            return problem(403,
                           "Forbidden",
                           "parameter exchange applies to PRINS, which this SEPP does "
                           "not support (TLS security only)",
                           "NEGOTIATION_NOT_ALLOWED",
                           policy.own_fqdn);
        });
    n32.add_route(
        "POST", std::string(kHandshakeRoot) + "/n32f-terminate", [&](const http2::Request& req) {
            sbi_gen::N32fContextInfo body;
            try {
                body = json::parse(req.body).get<sbi_gen::N32fContextInfo>();
            } catch (const std::exception& e) {
                return problem(400, "Bad Request", e.what(), "INVALID_MSG_FORMAT", policy.own_fqdn);
            }
            // n32fContextIds come from the PRINS parameter exchange only (5.2.3/5.2.4).
            return problem(404,
                           "Not Found",
                           "no N32-f context " + body.n32fContextId +
                               " (this SEPP negotiates TLS security, which has none)",
                           std::nullopt,
                           policy.own_fqdn);
        });
    n32.add_route(
        "POST", std::string(kHandshakeRoot) + "/n32f-error", [&](const http2::Request& req) {
            sbi_gen::N32fErrorInfo body;
            try {
                body = json::parse(req.body).get<sbi_gen::N32fErrorInfo>();
            } catch (const std::exception& e) {
                return problem(400, "Bad Request", e.what(), "INVALID_MSG_FORMAT", policy.own_fqdn);
            }
            // 5.2.5 step 2a: log that the N32-f message was not processed by the peer.
            spdlog::warn("sepp: peer reports N32-f message {} not processed: {}",
                         body.n32fMessageId,
                         body.n32fErrorType.value);
            http2::Response r;
            r.status = 204;
            return r;
        });

    // ---- N32-f, receiving SEPP (5.3.3, TS 29.500 6.1.4.3.4) ----------------------------------
    n32.add_route("*", "/**", [&](const http2::Request& req) {
        const auto via = header(req, "via").value_or("");
        const auto ctx = sepp::correlate(contexts.all(),
                                         req.peer_cert_dns_names,
                                         via,
                                         header(req, kHandshakeIdHeader),
                                         header(req, kPurposeHeader));
        if (!ctx) {
            refused->Add(1);
            return problem(403,
                           "Forbidden",
                           "N32-f request over TLS with no related N32-c context",
                           "CONTEXT_NOT_FOUND",
                           policy.own_fqdn);
        }
        const auto target = header(req, kTargetApiRoot);
        if (!target) {
            refused->Add(1);
            return problem(400,
                           "Bad Request",
                           "3gpp-Sbi-Target-apiRoot is required on N32-f",
                           "MANDATORY_IE_MISSING",
                           policy.own_fqdn);
        }
        if (sepp::network_domain_of_host(sepp::host_of(*target)) != own_domain) {
            refused->Add(1);
            return problem(400,
                           "Bad Request",
                           "3gpp-Sbi-Target-apiRoot " + *target + " is not in PLMN " + own_domain,
                           "MANDATORY_IE_INCORRECT",
                           policy.own_fqdn);
        }
        const auto [authority, prefix] = split_api_root(*target);
        // A target apiRoot without a port is the https default (e.g. one rebuilt from a telescopic
        // FQDN, which carries no port): "host" or "host:443" in config.
        auto route = local_nf_routes.find(authority);
        if (route == local_nf_routes.end() && authority.find(':') == std::string::npos) {
            route = local_nf_routes.find(authority + ":443");
        }
        if (route == local_nf_routes.end()) {
            refused->Add(1);
            return problem(504,
                           "Gateway Timeout",
                           "no route to " + authority + " in this PLMN",
                           "TARGET_NF_NOT_REACHABLE",
                           policy.own_fqdn);
        }
        forwarded->Add(1);
        // 5.3.3.3: the handshake id is removed by the receiving SEPP; the target apiRoot is
        // consumed here.
        return relay(client,
                     req,
                     route->second + prefix + req.path,
                     {kTargetApiRoot, kHandshakeIdHeader},
                     {},
                     policy.own_fqdn);
    });

    // ---- Nsepp_Telescopic_FQDN_Mapping (TS 29.573 5.4, ADR-0492) ------------------------------
    sbi.add_route("GET", "/nsepp-telescopic/v1/mapping", [&](const http2::Request& req) {
        const auto fqdn = req.query_params.find("foreign-fqdn");
        const auto label = req.query_params.find("telescopic-label");
        const bool has_fqdn = fqdn != req.query_params.end();
        const bool has_label = label != req.query_params.end();
        if (has_fqdn == has_label) {
            return problem(400,
                           "Bad Request",
                           "exactly one of foreign-fqdn and telescopic-label is required",
                           "INVALID_QUERY_PARAM",
                           policy.own_fqdn);
        }
        sbi_gen::TelescopicMapping mapping;
        if (has_fqdn) {
            // 5.4.2: only an FQDN of another PLMN has a telescopic form.
            const auto domain = sepp::network_domain_of_host(fqdn->second);
            if (!domain || *domain == own_domain) {
                return problem(400,
                               "Bad Request",
                               "foreign-fqdn " + fqdn->second + " is not an FQDN of another PLMN",
                               "INVALID_QUERY_PARAM",
                               policy.own_fqdn);
            }
            mapping.telescopicLabel = telescopic.label_for(fqdn->second);
            mapping.seppDomain = policy.own_fqdn;
        } else {
            const auto foreign = telescopic.foreign_for(label->second);
            if (!foreign) {
                return problem(404,
                               "Not Found",
                               "telescopic label " + label->second + " is not known to this SEPP",
                               "RESOURCE_URI_STRUCTURE_NOT_FOUND",
                               policy.own_fqdn);
            }
            mapping.foreignFqdn = *foreign;
        }
        return http2::Response::json(200, json(mapping).dump());
    });

    // ---- NF side, sending SEPP (TS 29.500 6.1.4.3.2 / 6.1.4.3.3 / 6.1.4.3.4) ------------------
    sbi.add_route("*", "/**", [&](const http2::Request& req) {
        // A telescopic FQDN in the request URI wins over a 3gpp-Sbi-Target-apiRoot header
        // (6.1.4.3.3 NOTE 1: the header is then ignored).
        std::optional<std::string> target;
        bool via_telescopic = false;
        if (const auto tl =
                sepp::telescopic_label_of_host(sepp::host_of(req.authority), policy.own_fqdn)) {
            const auto foreign = telescopic.foreign_for(*tl);
            if (!foreign) {
                return problem(404,
                               "Not Found",
                               "telescopic label " + *tl + " is not known to this SEPP",
                               "RESOURCE_URI_STRUCTURE_NOT_FOUND",
                               policy.own_fqdn);
            }
            target = "https://" + *foreign;
            via_telescopic = true;
        } else {
            target = header(req, kTargetApiRoot);
        }
        if (!target) {
            return problem(400,
                           "Bad Request",
                           "neither a telescopic FQDN nor 3gpp-Sbi-Target-apiRoot names the target",
                           "MANDATORY_IE_MISSING",
                           policy.own_fqdn);
        }
        const auto domain = sepp::network_domain_of_host(sepp::host_of(*target));
        if (!domain || *domain == own_domain) {
            return problem(400,
                           "Bad Request",
                           "3gpp-Sbi-Target-apiRoot " + *target + " is not in a remote PLMN",
                           "MANDATORY_IE_INCORRECT",
                           policy.own_fqdn);
        }
        const auto ctx = contexts.by_domain(*domain);
        if (!ctx) {
            return problem(504,
                           "Gateway Timeout",
                           "no N32 context with PLMN " + *domain,
                           "TARGET_NF_NOT_REACHABLE",
                           policy.own_fqdn);
        }
        if (!ctx->peer_target_api_root) {
            return problem(504,
                           "Gateway Timeout",
                           "the SEPP of " + *domain +
                               " does not support 3gpp-Sbi-Target-apiRoot (not built, ADR-0491)",
                           "TARGET_NF_NOT_REACHABLE",
                           policy.own_fqdn);
        }
        std::vector<std::pair<std::string, std::string>> extra;
        if (ctx->handshake_id) {
            extra.emplace_back(kHandshakeIdHeader, *ctx->handshake_id);
        }
        std::vector<std::string> drop;
        if (via_telescopic) {
            // 6.1.4.3.4 second case: the sending SEPP inserts the header, set to the target
            // apiRoot derived from the telescopic FQDN (any header the NF sent is ignored).
            drop.emplace_back(kTargetApiRoot);
            extra.emplace_back(kTargetApiRoot, *target);
        }
        forwarded->Add(1);
        // Both SEPPs support the header: HTTPS to the peer SEPP's apiRoot.
        auto api_root = ctx->n32_api_root;
        while (!api_root.empty() && api_root.back() == '/') {
            api_root.pop_back();
        }
        auto resp = relay(client, req, api_root + req.path, drop, extra, policy.own_fqdn);
        if (via_telescopic) {
            // 6.1.4.3.2: a link relation URI of the other PLMN goes back to the NF in telescopic
            // form (here: the Location header; URIs inside bodies are not rewritten, ADR-0492).
            const auto loc = resp.headers.find("location");
            if (loc != resp.headers.end()) {
                const std::string host = sepp::host_of(loc->second);
                if (sepp::network_domain_of_host(host) &&
                    *sepp::network_domain_of_host(host) != own_domain) {
                    const auto at = loc->second.find(host);
                    std::string rewritten = loc->second;
                    rewritten.replace(
                        at, host.size(), telescopic.label_for(host) + "." + policy.own_fqdn);
                    loc->second = rewritten;
                }
            }
        }
        return resp;
    });

    // ---- initiating SEPP: 5.2.2 towards every peer configured with initiate -------------------
    std::atomic<bool> running{true};
    const std::string instance_id = sbi_core::generate_uuid_v4();
    std::thread nrf_thread(run_nrf_lifecycle,
                           instance_id,
                           nrf_base,
                           policy.own_fqdn,
                           policy.own_mcc,
                           policy.own_mnc,
                           std::cref(running));
    std::thread initiator([&] {
        http2::Client c(sepp_tls());
        while (running.load()) {
            for (const auto& peer : policy.peers) {
                if (!peer.initiate || contexts.by_peer(peer.fqdn)) {
                    continue;
                }
                const auto id = random_handshake_id();
                http2::ClientRequest req;
                req.method = "POST";
                req.url = peer.n32_api_root + kHandshakeRoot + "/exchange-capability";
                req.headers.emplace("content-type", "application/json");
                req.body = json(sepp::make_request(policy, peer, id, false)).dump();
                contexts.set_outgoing(peer.fqdn, true);
                const auto r = c.send(req);
                contexts.set_outgoing(peer.fqdn, false);
                if (!r || r->status != 200) {
                    spdlog::warn("sepp: exchange-capability to {} failed ({})",
                                 peer.fqdn,
                                 r ? std::to_string(r->status) + " " + r->body : r.error());
                    continue;
                }
                try {
                    const auto rsp = json::parse(r->body).get<sbi_gen::SecNegotiateRspData>();
                    if (const auto ctx = sepp::context_from_response(peer, rsp, id)) {
                        contexts.put(*ctx);
                        handshakes->Add(1);
                        spdlog::info("sepp: N32 context with {} (TLS, handshake id {})",
                                     ctx->peer_fqdn,
                                     ctx->handshake_id.value_or("-"));
                    } else {
                        spdlog::error("sepp: {} selected {}, not TLS -- no context",
                                      peer.fqdn,
                                      rsp.selectedSecCapability.value);
                    }
                } catch (const std::exception& e) {
                    spdlog::error(
                        "sepp: bad exchange-capability response from {}: {}", peer.fqdn, e.what());
                }
            }
            for (int i = 0; i < retry_seconds * 10 && running.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });

    // On shutdown, tear every N32-f TLS connection down (5.2.2: capability "NONE").
    sbi_core::on_shutdown_signal([&] {
        running = false;
        http2::Client c(sepp_tls());
        for (const auto& ctx : contexts.all()) {
            const auto peer = std::ranges::find_if(
                policy.peers, [&](const sepp::PeerConfig& p) { return p.fqdn == ctx.peer_fqdn; });
            if (peer == policy.peers.end()) {
                continue;
            }
            http2::ClientRequest req;
            req.method = "POST";
            req.url = ctx.n32_api_root + kHandshakeRoot + "/exchange-capability";
            req.headers.emplace("content-type", "application/json");
            req.body = json(sepp::make_request(policy, *peer, ctx.handshake_id, true)).dump();
            (void)c.send(req);
        }
    });

    sbi.start();
    n32.start();
    spdlog::info("sepp: NF side https://{}:{}, N32 https://{}:{} (TLS 1.3 + mTLS)",
                 bind,
                 port,
                 bind,
                 n32_port);
    sbi_core::run_multi_threaded(ioc);
    running = false;
    initiator.join();
    nrf_thread.join();
    return 0;
}
