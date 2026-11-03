#include "sbi_core/oauth2_client.hpp"

#include "sbi_core/api_catalog.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <iomanip>
#include <sstream>

#include "no_destroy.hpp"

namespace sbi_core {

namespace {

std::string form_urlencode(const std::string& value) {
    std::ostringstream out;
    for (const char raw_c : value) {
        const auto c = static_cast<unsigned char>(raw_c);
        if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
            out << c;
        } else if (c == ' ') {
            out << '+';
        } else {
            out << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(c);
        }
    }
    return out.str();
}

std::string problem_details_text(long status, const nlohmann::json& pd) {
    std::string out = "ProblemDetails " + std::to_string(status);
    if (const auto it = pd.find("cause"); it != pd.end() && it->is_string()) {
        out += " " + it->get<std::string>();
    }
    if (const auto it = pd.find("detail"); it != pd.end() && it->is_string()) {
        out += ": " + it->get<std::string>();
    } else if (const auto t = pd.find("title"); t != pd.end() && t->is_string()) {
        out += ": " + t->get<std::string>();
    }
    if (const auto it = pd.find("invalidParams"); it != pd.end() && it->is_array()) {
        std::string params;
        for (const auto& p : *it) {
            if (p.is_object() && p.contains("param") && p["param"].is_string()) {
                params += (params.empty() ? "" : ", ") + p["param"].get<std::string>();
            }
        }
        if (!params.empty()) {
            out += " [invalidParams: " + params + "]";
        }
    }
    return out;
}

bool looks_like_problem_details(const nlohmann::json& j) {
    return j.contains("cause") || j.contains("title") || j.contains("detail") ||
           j.contains("status");
}

} // namespace

bool is_access_token_err_code(std::string_view error) {
    for (const auto code : kAccessTokenErrCodes) {
        if (code == error) {
            return true;
        }
    }
    return false;
}

std::string describe_token_refusal(long status, const std::string& body) {
    const std::string raw = "status " + std::to_string(status) + ": " + body;
    const auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
    const bool object = j.is_object();
    if (status == 400 || status == 401) {
        if (object && j.contains("error") && j["error"].is_string()) {
            const auto error = j["error"].get<std::string>();
            std::string out = "AccessTokenErr " + error;
            if (!is_access_token_err_code(error)) {
                out += " (not a TS 29.510 AccessTokenErr value)";
            }
            if (const auto it = j.find("error_description"); it != j.end() && it->is_string()) {
                out += ": " + it->get<std::string>();
            }
            if (const auto it = j.find("error_uri"); it != j.end() && it->is_string()) {
                out += " (" + it->get<std::string>() + ")";
            }
            return out;
        }
        if (status == 400 && object && looks_like_problem_details(j)) {
            return problem_details_text(status, j); // SCP/SEPP-originated (TS 29.510)
        }
        return "non-conformant token refusal, no AccessTokenErr body (TS 29.510 Table "
               "6.3.4.2.2-2): " +
               raw;
    }
    if (status == 403 && object && looks_like_problem_details(j)) {
        return problem_details_text(status, j);
    }
    return raw;
}

OAuth2Client::OAuth2Client(http2::Client& http_client,
                           std::string nrf_token_endpoint,
                           std::string nf_instance_id,
                           std::string scope,
                           std::string target_nf_type)
    : http_client_(http_client), nrf_token_endpoint_(std::move(nrf_token_endpoint)),
      nf_instance_id_(std::move(nf_instance_id)), scope_(std::move(scope)),
      target_nf_type_(std::move(target_nf_type)) {}

tl::expected<std::string, std::string> OAuth2Client::get_bearer_token() {
    std::lock_guard<std::mutex> lock(mutex_);

    constexpr auto kSafetyMargin = std::chrono::seconds(10);
    if (cached_token_.has_value() &&
        std::chrono::steady_clock::now() + kSafetyMargin < cached_token_->expires_at) {
        return cached_token_->access_token;
    }

    auto token = fetch_token();
    if (!token.has_value()) {
        return tl::unexpected(token.error());
    }
    cached_token_ = *token;
    return cached_token_->access_token;
}

tl::expected<OAuth2Token, std::string> OAuth2Client::fetch_token() {
    http2::ClientRequest req;
    req.method = "POST";
    req.url = nrf_token_endpoint_;
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");

    std::ostringstream body;
    body << "grant_type=" << form_urlencode("client_credentials");
    body << "&nfInstanceId=" << form_urlencode(nf_instance_id_);
    body << "&scope=" << form_urlencode(scope_);
    body << "&targetNfType=" << form_urlencode(target_nf_type_);
    req.body = body.str();

    auto resp = http_client_.send(req);
    if (!resp.has_value()) {
        return tl::unexpected("oauth2 token request failed: " + resp.error());
    }
    if (resp->status != 200) {
        return tl::unexpected("oauth2 token request refused: " +
                              describe_token_refusal(resp->status, resp->body));
    }

    OAuth2Token token;
    try {
        const auto json = nlohmann::json::parse(resp->body);
        token.access_token = json.at("access_token").get<std::string>();
        token.token_type = json.at("token_type").get<std::string>();
        const auto expires_in =
            json.contains("expires_in") ? json.at("expires_in").get<long>() : 3600L;
        token.expires_at = std::chrono::steady_clock::now() + std::chrono::seconds(expires_in);
    } catch (const nlohmann::json::exception& e) {
        return tl::unexpected(std::string("failed to parse AccessTokenRsp: ") + e.what());
    }

    return token;
}

TokenSource::TokenSource(http2::Client& nrf_client,
                         std::string nrf_token_endpoint,
                         std::string nf_instance_id,
                         std::chrono::seconds retry_after)
    : nrf_client_(nrf_client), nrf_token_endpoint_(std::move(nrf_token_endpoint)),
      nf_instance_id_(std::move(nf_instance_id)), retry_after_(retry_after) {}

std::optional<std::string> TokenSource::token_for(const std::string& url) {
    const auto info = api_catalog::find(api_catalog::api_name_of(url));
    // No producer type, or no scope to ask for (AccessTokenReq.scope is mandatory): no token.
    if (!info || info->nf_type.empty() || info->scope.empty()) {
        return std::nullopt;
    }
    const std::pair<std::string, std::string> key{std::string(info->scope),
                                                  std::string(info->nf_type)};
    OAuth2Client* client = nullptr;
    {
        std::lock_guard lock(mutex_);
        auto& entry = entries_[key];
        if (std::chrono::steady_clock::now() < entry.retry_at) {
            return std::nullopt;
        }
        if (!entry.client) {
            entry.client = std::make_unique<OAuth2Client>(
                nrf_client_, nrf_token_endpoint_, nf_instance_id_, key.first, key.second);
        }
        client = entry.client.get(); // stable: entries are never erased
    }
    auto token = client->get_bearer_token();
    if (token.has_value()) {
        return *token;
    }
    spdlog::warn("sbi-core: no access token for {} ({}): {}; sending without one",
                 key.first,
                 key.second,
                 token.error());
    std::lock_guard lock(mutex_);
    entries_[key].retry_at = std::chrono::steady_clock::now() + retry_after_;
    return std::nullopt;
}

void TokenSource::clear_backoff() {
    std::lock_guard lock(mutex_);
    for (auto& [key, entry] : entries_) {
        entry.retry_at = {};
    }
}

void attach_tokens(http2::Client& client, TokenSource& source) {
    client.set_token_source([&source](const std::string& url) { return source.token_for(url); });
}

namespace {
// Never destroyed (no_destroy.hpp): an NF's detached NRF lifecycle thread calls
// clear_default_token_backoff() after NFRegister, which can land while the process exits.
std::mutex& default_source_mutex() {
    static detail::NoDestroy<std::mutex> m;
    return m.get();
}
// The TokenSource install_default_token_source installed, for clear_default_token_backoff.
std::shared_ptr<TokenSource>& default_source() {
    static detail::NoDestroy<std::shared_ptr<TokenSource>> source;
    return source.get();
}
} // namespace

void install_default_token_source(const http2::TlsConfig& tls,
                                  std::string nrf_token_endpoint,
                                  std::string nf_instance_id) {
    // Owned by the installed function, so a send() still holding the previous default keeps it
    // alive. The NRF client itself falls back to this source too; the token endpoint is not in
    // the API catalog, so its requests are sent without a token.
    struct State {
        State(const http2::TlsConfig& tls, std::string endpoint, std::string instance_id)
            : nrf_client(tls), source(nrf_client, std::move(endpoint), std::move(instance_id)) {}
        http2::Client nrf_client;
        TokenSource source;
    };
    auto state =
        std::make_shared<State>(tls, std::move(nrf_token_endpoint), std::move(nf_instance_id));
    {
        const std::lock_guard lock(default_source_mutex());
        default_source() = std::shared_ptr<TokenSource>(state, &state->source);
    }
    http2::Client::set_default_token_source(
        [state](const std::string& url) { return state->source.token_for(url); });
}

void clear_default_token_backoff() {
    std::shared_ptr<TokenSource> source;
    {
        const std::lock_guard lock(default_source_mutex());
        source = default_source();
    }
    if (source) {
        source->clear_backoff();
    }
}

} // namespace sbi_core
