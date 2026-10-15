// nfs/aanf: AAnF -- AKMA Anchor Function, increment 1 (ADR-0495).
//
// Stage 2: TS 33.535 V19.0.0 (specs/3gpp/TS_33.535_j00.txt). Stage 3: TS 29.535 V19.5.0 and
// TS29535_Naanf_AKMA.yaml (specs/5G_APIs-REL-19, forge.3gpp.org REL-19, commit bca84b6; request /
// response bodies from TS29522_AKMA.yaml), DTOs generated into sbi_gen. API root
// {apiRoot}/naanf-akma/v1:
//
//   POST /register-anchorkey       Naanf_AKMA_AnchorKey_Register (TS 33.535 6.1 step 4-5): the
//                                  AUSF's A-KID + KAKMA for a SUPI; the latest replaces the older
//                                  (6.1 NOTE 1a)
//   POST /retrieve-applicationkey  Naanf_AKMA_ApplicationKey_Get / _AnonUser_Get (6.2.1, 6.2.2,
//                                  6.3): KAF = KDF(KAKMA, AF_ID) (Annex A.4), its expiry, and the
//                                  SUPI unless anonymous or not authorized
//   POST /remove-context           Naanf_AKMA_Context_Remove (6.6)
//
// AKMA contexts live in Valkey, shared by every replica.
//
// DISCLOSED, not hidden:
//   * No roaming check (6.2.1 steps 5-7, Nudm_EventExposure RoamingStatusReport): the
//     RoamingRestriction feature is not supported, so AKMA is provided whatever the UE's roaming
//     status. No GPSI (steps 3-4, Nudm_SDM_Get; AKMA_GPSI_Support feature not supported).
//   * No AKMA service-disable notification (6.8): notifUri is accepted and ignored.
//   * The AF authorization of 6.2.1 step 2 is the OAuth2 token (when present) plus the configured
//     `allowed_af_fqdns` list (empty = any AF).
//   * kAkma / kaf are hexadecimal strings: TS 29.535 / 29.522 type them "string" without an
//     encoding; hex follows the key fields of TS 29.509 (kseaf, kausf).

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/io_context_pool.hpp"
#include "sbi_core/jwt.hpp"
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

#include <chrono>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

#include "TS29522_AKMA.hpp"
#include "TS29535_Naanf_AKMA.hpp"
#include "aka_crypto/akma.hpp"
#include "aka_crypto/hex.hpp"
#include "akma_store.hpp"
#include "nf_config/nf_config.hpp"
#include "nf_config/redis.hpp"

namespace {

using nlohmann::json;
namespace http2 = sbi_core::http2;

#ifndef CERTS_DIR
#error "CERTS_DIR must be defined by CMake (see nfs/aanf/CMakeLists.txt)"
#endif
#ifndef CONFIG_DIR
#error "CONFIG_DIR must be defined by CMake (see nfs/aanf/CMakeLists.txt)"
#endif

constexpr const char* kApiRoot = "/naanf-akma/v1";
// Must match nfs/nrf/src/main.cpp's kNrfInstanceId exactly -- see docs/DECISIONS.md ADR-0018.
constexpr const char* kNrfInstanceId = "5ba9a927-1d31-4c8e-8a10-000000000001";
constexpr const char* kSupiScope = "naanf-akma:applicationkeyget:supi-access";

http2::TlsConfig aanf_tls() {
    return http2::TlsConfig{
        .cert_path = CERTS_DIR "/aanf/cert.pem",
        .key_path = CERTS_DIR "/aanf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
}

http2::Response problem(int status,
                        const std::string& title,
                        const std::string& detail,
                        const std::optional<std::string>& cause = std::nullopt) {
    http2::Response r;
    r.status = status;
    r.headers.emplace("content-type", "application/problem+json");
    r.body = json(sbi_core::make_problem_details(status, title, detail, cause)).dump();
    return r;
}

// A missing Authorization header is not itself a 401: the YAML's `security: [{}, ...]` (same
// reading as every other NF here, see nfs/nrf/src/main.cpp).
std::optional<sbi_core::jwt::VerifyResult> check_bearer(const http2::Request& req,
                                                        sbi_core::jwt::Verifier& verifier) {
    const auto it = req.headers.find("authorization");
    if (it == req.headers.end()) {
        return std::nullopt;
    }
    constexpr std::string_view kPrefix = "Bearer ";
    if (it->second.size() <= kPrefix.size() ||
        it->second.compare(0, kPrefix.size(), kPrefix) != 0) {
        sbi_core::jwt::VerifyResult r;
        r.error = "Authorization header present but not a Bearer token";
        return r;
    }
    return verifier.verify(it->second.substr(kPrefix.size()));
}

std::string rfc3339(std::chrono::system_clock::time_point t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
    gmtime_r(&tt, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

// The FQDN part of a TS 29.522 AfId "<FQDN>.<10 hex digits>".
std::string af_fqdn(const std::string& af_id) {
    return af_id.substr(0, af_id.rfind('.'));
}

void run_nrf_lifecycle(const std::string& instance_id, const std::string& nrf_base) {
    http2::Client http_client(aanf_tls());
    sbi_core::OAuth2Client oauth(
        http_client, nrf_base + "/oauth2/token", instance_id, "nnrf-nfm", "NRF");
    constexpr int kHeartbeatSeconds = 30;
    const json profile{{"nfInstanceId", instance_id},
                       {"nfType", "AANF"},
                       {"nfStatus", "REGISTERED"},
                       {"ipv4Addresses", json::array({"127.0.0.1"})},
                       {"heartBeatTimer", kHeartbeatSeconds}};
    while (true) {
        if (auto token = oauth.get_bearer_token()) {
            http2::ClientRequest put;
            put.method = "PUT";
            put.url = nrf_base + "/nnrf-nfm/v1/nf-instances/" + instance_id;
            put.headers.emplace("content-type", "application/json");
            put.headers.emplace("authorization", "Bearer " + *token);
            put.headers.emplace(
                sbi_core::headers::kSenderTimestamp,
                sbi_core::headers::format_sender_timestamp(std::chrono::system_clock::now()));
            put.body = profile.dump();
            if (const auto r = http_client.send(put); r && (r->status == 200 || r->status == 201)) {
                spdlog::info("aanf: registered with NRF (HTTP {})", r->status);
                break;
            }
        }
        spdlog::warn("aanf: NRF registration attempt failed, retrying in 5s");
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(kHeartbeatSeconds / 2));
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
            spdlog::warn("aanf: NRF heartbeat failed");
        }
    }
}

} // namespace

int main() {
    sbi_core::init_logging("aanf");
    sbi_core::init_tracing("aanf");

    const auto config = nf_config::load("aanf", CONFIG_DIR);
    const auto port = nf_config::require<unsigned short>(config, "port", "AANF_PORT");
    const auto bind = nf_config::require<std::string>(config, "sbi_bind_address");
    const auto metrics_bind_address = nf_config::require<std::string>(
        config, "metrics_bind_address", "AANF_METRICS_BIND_ADDRESS");
    const auto nrf_base =
        nf_config::require<std::string>(config, "nrf_base_url", "AANF_NRF_BASE_URL");
    const auto redis_url = nf_config::require<std::string>(config, "redis_url", "AANF_REDIS_URL");
    const auto kaf_lifetime = std::chrono::seconds(
        nf_config::require<int>(config, "kaf_lifetime_seconds", "AANF_KAF_LIFETIME_SECONDS"));
    // Local policy for an AF calling without a token: whether the SUPI may be returned (6.2.1
    // step 8, "Whether to send SUPI or GPSI is determined by AAnF based on the local policy").
    const bool supi_without_token = config.value("supi_to_af_without_token", false);
    const auto allowed_afs = config.value("allowed_af_fqdns", std::vector<std::string>{});

    sbi_core::init_metrics(metrics_bind_address);
    auto meter = sbi_core::get_meter("aanf");
    auto registered = meter->CreateUInt64Counter("aanf_anchor_keys_registered_total",
                                                 "Naanf_AKMA_AnchorKey_Register requests stored");
    auto kafs = meter->CreateUInt64Counter("aanf_application_keys_total",
                                           "KAF delivered by Naanf_AKMA_ApplicationKey_Get");

    auto redis = nf_config::connect_redis_or_die(redis_url, "aanf");
    aanf::AkmaStore store(redis);
    sbi_core::jwt::Verifier verifier(CERTS_DIR "/nrf-jwt/public.pem", kNrfInstanceId);
    const std::string instance_id = sbi_core::generate_uuid_v4();
    spdlog::info("aanf: starting, nfInstanceId={}", instance_id);

    boost::asio::io_context ioc;
    http2::Server server(ioc, bind, port, aanf_tls());

    // ---- Naanf_AKMA_AnchorKey_Register (TS 29.535 5.2.2.2) -------------------------------------
    server.add_route(
        "POST", std::string(kApiRoot) + "/register-anchorkey", [&](const http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth && !auth->valid) {
                return problem(401, "Unauthorized", auth->error);
            }
            sbi_gen::AkmaKeyInfo body;
            try {
                body = json::parse(req.body).get<sbi_gen::AkmaKeyInfo>();
            } catch (const std::exception& e) {
                return problem(400, "Bad Request", e.what(), "INVALID_MSG_FORMAT");
            }
            const auto kakma = aka_crypto::from_hex<32>(body.kAkma);
            if (!kakma || body.aKId.empty() || body.aKId.find('@') == std::string::npos) {
                return problem(400,
                               "Bad Request",
                               "kAkma must be 64 hexadecimal digits and aKId an NAI",
                               "MANDATORY_IE_INCORRECT");
            }
            if (!body.supi || body.gpsi) {
                // The YAML's oneOf allows GPSI; the AAnF keys contexts by SUPI (6.6), which the
                // AUSF always has.
                return problem(400,
                               "Bad Request",
                               "the AKMA context is registered by SUPI",
                               "MANDATORY_IE_MISSING");
            }
            store.put(aanf::AkmaContext{*body.supi, body.aKId, *kakma});
            registered->Add(1);
            spdlog::info("aanf: AKMA context for {} registered (A-KID {})", *body.supi, body.aKId);
            return http2::Response::json(200, json(body).dump());
        });

    // ---- Naanf_AKMA_ApplicationKey_Get / _AnonUser_Get (TS 29.535 5.2.2.3) ---------------------
    server.add_route(
        "POST", std::string(kApiRoot) + "/retrieve-applicationkey", [&](const http2::Request& req) {
            const auto auth = check_bearer(req, verifier);
            if (auth && !auth->valid) {
                return problem(401, "Unauthorized", auth->error);
            }
            sbi_gen::AkmaAfKeyRequest body;
            try {
                body = json::parse(req.body).get<sbi_gen::AkmaAfKeyRequest>();
            } catch (const std::exception& e) {
                return problem(400, "Bad Request", e.what(), "INVALID_MSG_FORMAT");
            }
            const auto af_id = aka_crypto::af_id_octets(body.afId);
            if (!af_id) {
                return problem(400,
                               "Bad Request",
                               "afId must be \"<FQDN>.<Ua* security protocol id: 10 hex digits>\"",
                               "MANDATORY_IE_INCORRECT");
            }
            if (!allowed_afs.empty() &&
                std::ranges::find(allowed_afs, af_fqdn(body.afId)) == allowed_afs.end()) {
                return problem(403, "Forbidden", "AF " + af_fqdn(body.afId) + " is not authorized");
            }
            const auto ctx = store.by_akid(body.aKId);
            if (!ctx) {
                return problem(
                    403, "Forbidden", "no KAKMA for A-KID " + body.aKId, "K_AKMA_NOT_PRESENT");
            }
            const auto kaf = aka_crypto::derive_kaf(ctx->kakma, *af_id);
            sbi_gen::AkmaAfKeyData out;
            out.kaf = aka_crypto::to_hex(kaf);
            out.expiry = rfc3339(std::chrono::system_clock::now() + kaf_lifetime);
            const bool anonymous = body.anonInd.value_or(false);
            const bool supi_allowed =
                auth ? auth->scope.find(kSupiScope) != std::string::npos : supi_without_token;
            if (!anonymous && supi_allowed) {
                out.supi = ctx->supi;
            }
            kafs->Add(1);
            return http2::Response::json(200, json(out).dump());
        });

    // ---- Naanf_AKMA_Context_Remove (TS 29.535 5.2.2.4) -----------------------------------------
    server.add_route(
        "POST", std::string(kApiRoot) + "/remove-context", [&](const http2::Request& req) {
            if (auto auth = check_bearer(req, verifier); auth && !auth->valid) {
                return problem(401, "Unauthorized", auth->error);
            }
            sbi_gen::CtxRemove body;
            try {
                body = json::parse(req.body).get<sbi_gen::CtxRemove>();
            } catch (const std::exception& e) {
                return problem(400, "Bad Request", e.what(), "INVALID_MSG_FORMAT");
            }
            if (!body.supi) {
                return problem(400, "Bad Request", "supi is required", "MANDATORY_IE_MISSING");
            }
            if (!store.remove_supi(*body.supi)) {
                return problem(404,
                               "Not Found",
                               "no AKMA context for " + *body.supi,
                               "AKMA_CONTEXT_NOT_FOUND");
            }
            http2::Response r;
            r.status = 204;
            return r;
        });

    std::thread(run_nrf_lifecycle, instance_id, nrf_base).detach();
    server.start();
    spdlog::info("aanf: listening on https://{}:{} (TLS 1.3 + mTLS)", bind, port);
    sbi_core::run_multi_threaded(ioc);
    return 0;
}
