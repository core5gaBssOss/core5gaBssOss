#include "prins_codec.hpp"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <memory>

namespace sepp {

namespace {

using json = nlohmann::json;

// TS 33.501 5.9.3.3: confidentiality protected "irrespective of the Data-type Encryption Policy".
const std::set<std::string> kAlwaysEncryptedTypes{
    "AUTHENTICATION_MATERIAL", "KEY_MATERIAL", "LOCATION", "AUTHORIZATION_TOKEN"};

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        const auto pos = s.find(sep, start);
        out.push_back(s.substr(start, pos - start));
        if (pos == std::string::npos) {
            return out;
        }
        start = pos + 1;
    }
}

std::string escape_pointer_token(const std::string& t) {
    std::string out;
    for (const char c : t) {
        if (c == '~') {
            out += "~0";
        } else if (c == '/') {
            out += "~1";
        } else {
            out += c;
        }
    }
    return out;
}

// RFC 6901 leaves of a JSON value (TS 29.573 6.2.5.2.8: objects flattened to leaf attributes).
void flatten(const json& j,
             const std::string& ptr,
             std::vector<std::pair<std::string, json>>& out) {
    if (j.is_object() && !j.empty()) {
        for (const auto& [k, v] : j.items()) {
            flatten(v, ptr + "/" + escape_pointer_token(k), out);
        }
    } else if (j.is_array() && !j.empty()) {
        for (std::size_t i = 0; i < j.size(); ++i) {
            flatten(j[i], ptr + "/" + std::to_string(i), out);
        }
    } else {
        out.emplace_back(ptr, j);
    }
}

bool under(const std::string& ptr, const std::string& selected) {
    return ptr == selected ||
           (ptr.size() > selected.size() && ptr.compare(0, selected.size(), selected) == 0 &&
            ptr[selected.size()] == '/');
}

std::string pointer_of(const std::string& ie) {
    return !ie.empty() && ie.front() == '/' ? ie : "/" + escape_pointer_token(ie);
}

std::string strip_braces(const std::string& s) {
    return s.size() >= 2 && s.front() == '{' && s.back() == '}' ? s.substr(1, s.size() - 2) : s;
}

json enc_index(std::size_t i) {
    return json{{"encBlockIndex", i}};
}

std::optional<std::size_t> as_enc_index(const json& v) {
    if (v.is_object() && v.size() == 1 && v.contains("encBlockIndex") &&
        v.at("encBlockIndex").is_number_unsigned()) {
        return v.at("encBlockIndex").get<std::size_t>();
    }
    return std::nullopt;
}

const EVP_CIPHER* gcm(const std::string& cipher) {
    if (cipher == "A128GCM") {
        return EVP_aes_128_gcm();
    }
    if (cipher == "A256GCM") {
        return EVP_aes_256_gcm();
    }
    return nullptr;
}

std::vector<std::uint8_t> bytes(const std::string& s) {
    return {s.begin(), s.end()};
}

struct CtxFree {
    void operator()(EVP_CIPHER_CTX* c) const { EVP_CIPHER_CTX_free(c); }
};

} // namespace

// SUS-03 (ADR-0530): every array index in `ptr` addresses an existing element or the next one, so
// reconstructing a body from iePaths cannot be made to allocate a huge array.
bool ie_path_within_bounds(const json& body, const std::string& ptr) {
    const json* node = &body;
    std::size_t pos = 1; // past the leading '/'
    while (pos <= ptr.size()) {
        const std::size_t next = ptr.find('/', pos);
        const std::string token =
            ptr.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        const bool numeric = !token.empty() && std::ranges::all_of(token, [](unsigned char c) {
            return std::isdigit(c) != 0;
        });
        if (numeric) {
            const std::size_t size = node != nullptr && node->is_array() ? node->size() : 0;
            if (token.size() > 9 || std::stoul(token) > size) {
                return false;
            }
        }
        if (node != nullptr) {
            if (numeric && node->is_array() && std::stoul(token) < node->size()) {
                node = &(*node)[std::stoul(token)];
            } else if (!numeric && node->is_object() && node->contains(token)) {
                node = &node->at(token);
            } else {
                node = nullptr; // the rest of the path is created fresh
            }
        }
        if (next == std::string::npos) {
            break;
        }
        pos = next + 1;
    }
    return true;
}

JweKeys derive_keys(const std::vector<std::uint8_t>& master_key,
                    const std::string& n32f_context_id,
                    KeyUse use,
                    const std::string& jwe_cipher) {
    const char* name = use == KeyUse::ParallelRequest    ? "parallel_request"
                       : use == KeyUse::ParallelResponse ? "parallel_response"
                       : use == KeyUse::ReverseRequest   ? "reverse_request"
                                                         : "reverse_response";
    const std::size_t key_len = jwe_cipher == "A256GCM" ? 32 : 16;
    JweKeys k;
    k.key = n32_kdf(master_key, n32f_context_id, std::string(name) + "_key", key_len);
    k.iv_salt = n32_kdf(master_key, n32f_context_id, std::string(name) + "_iv_salt", 8);
    return k;
}

KeyUse key_use(bool sender_is_initiator, bool is_response) {
    // The "parallel" session has the N32-c initiator as HTTP client: its requests, and the
    // responder's responses to them. The "reverse" session is the other way round.
    const bool parallel = sender_is_initiator != is_response;
    if (parallel) {
        return is_response ? KeyUse::ParallelResponse : KeyUse::ParallelRequest;
    }
    return is_response ? KeyUse::ReverseResponse : KeyUse::ReverseRequest;
}

Selection select(const std::optional<sbi_gen::ProtectionPolicy>& policy,
                 const std::string& method,
                 const std::string& path_without_query,
                 bool response) {
    Selection sel;
    sel.headers.insert("authorization"); // AUTHORIZATION_TOKEN: always (5.9.3.3)
    if (!policy) {
        return sel;
    }
    std::set<std::string> types(kAlwaysEncryptedTypes);
    if (policy->dataTypeEncPolicy) {
        for (const auto& t : *policy->dataTypeEncPolicy) {
            types.insert(t.value);
        }
    }
    const auto path = split(path_without_query, '/');
    for (const auto& mapping : policy->apiIeMappingList) {
        if (mapping.apiMethod.value != method) {
            continue;
        }
        const json sig = mapping.apiSignature;
        if (!sig.is_string()) {
            continue; // a CallbackName: notifications are matched by 3gpp-Sbi-Callback (not built)
        }
        const auto tmpl = split(sig.get<std::string>(), '/');
        if (tmpl.size() != path.size()) {
            continue;
        }
        bool match = true;
        for (std::size_t i = 0; i < tmpl.size() && match; ++i) {
            match = tmpl[i] == path[i] || (tmpl[i].size() > 2 && tmpl[i].front() == '{');
        }
        if (!match) {
            continue;
        }
        for (const auto& ie : mapping.IeList) {
            if (!types.contains(ie.ieType.value)) {
                continue;
            }
            const auto& name = response ? ie.rspIe : ie.reqIe;
            if (!name) {
                continue;
            }
            const auto& loc = ie.ieLoc.value;
            if (loc == "BODY") {
                sel.body_pointers.push_back(pointer_of(*name));
            } else if (loc == "HEADER") {
                std::string h = *name;
                std::ranges::transform(h, h.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                sel.headers.insert(h);
            } else if (loc == "URI_PATH" && !response) {
                for (std::size_t i = 0; i < tmpl.size(); ++i) {
                    if (strip_braces(tmpl[i]) == strip_braces(*name) && tmpl[i].front() == '{') {
                        sel.path_segments.insert(i);
                    }
                }
            }
            // URI_PARAM and MULTIPART_BINARY: not built (ADR-0494).
        }
    }
    return sel;
}

std::string base64url_encode(const std::vector<std::uint8_t>& in) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const std::uint32_t n = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
        out += kAlphabet[n & 63];
    }
    if (i + 1 == in.size()) {
        const std::uint32_t n = in[i] << 16;
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
    } else if (i + 2 == in.size()) {
        const std::uint32_t n = (in[i] << 16) | (in[i + 1] << 8);
        out += kAlphabet[(n >> 18) & 63];
        out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> base64url_decode(const std::string& in) {
    const auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }
        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }
        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }
        if (c == '-') {
            return 62;
        }
        if (c == '_') {
            return 63;
        }
        return -1;
    };
    if (in.size() % 4 == 1) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (const char c : in) {
        const int v = value(c);
        if (v < 0) {
            return std::nullopt;
        }
        acc = (acc << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return out;
}

tl::expected<sbi_gen::FlatJweJson, std::string> protect(const HttpMessage& message,
                                                        const Selection& selection,
                                                        const std::string& jwe_cipher,
                                                        const JweKeys& keys,
                                                        std::uint32_t seq,
                                                        const std::string& n32f_context_id,
                                                        const std::string& message_id) {
    const EVP_CIPHER* cipher = gcm(jwe_cipher);
    if (cipher == nullptr || keys.iv_salt.size() != 8) {
        return tl::unexpected("unsupported JWE cipher suite " + jwe_cipher);
    }
    std::vector<json> to_encrypt;
    sbi_gen::DataToIntegrityProtectBlock block;

    sbi_gen::MetaData meta;
    meta.n32fContextId = n32f_context_id;
    meta.messageId = message_id;
    meta.authorizedIpxId = "NULL"; // no RI is authorized to modify (6.2.5.2.9)
    block.metaData = meta;

    if (message.is_response()) {
        block.statusLine = std::to_string(message.status);
    } else {
        sbi_gen::RequestLine line;
        line.method.value = message.method;
        line.scheme.value = message.scheme;
        line.authority = message.authority;
        line.protocolVersion = "2";
        const auto q = message.path.find('?');
        auto segments = split(message.path.substr(0, q), '/');
        for (const auto i : selection.path_segments) {
            if (i < segments.size()) {
                to_encrypt.emplace_back(segments[i]);
                segments[i] = enc_index(to_encrypt.size() - 1).dump();
                line.pathQueryProtectInd = std::vector<sbi_gen::IeLocation>{{"URI_PATH"}};
            }
        }
        std::string path;
        for (std::size_t i = 0; i < segments.size(); ++i) {
            path += (i == 0 ? "" : "/") + segments[i];
        }
        line.path = path;
        if (q != std::string::npos) {
            line.queryFragment = message.path.substr(q + 1);
        }
        block.requestLine = line;
    }

    std::vector<sbi_gen::HttpHeader> headers;
    for (const auto& [name, value] : message.headers) {
        sbi_gen::HttpHeader h;
        h.header = name;
        if (selection.headers.contains(name)) {
            to_encrypt.emplace_back(value);
            h.value = enc_index(to_encrypt.size() - 1);
        } else {
            h.value = json(value);
        }
        headers.push_back(std::move(h));
    }
    if (!headers.empty()) {
        block.headers = std::move(headers);
    }

    if (!message.body.empty()) {
        const json body = json::parse(message.body, nullptr, false);
        if (body.is_discarded()) {
            return tl::unexpected("the message body is not JSON (multipart / binary content is not "
                                  "supported by this SEPP)");
        }
        std::vector<std::pair<std::string, json>> leaves;
        flatten(body, "", leaves);
        std::vector<sbi_gen::HttpPayload> payload;
        for (auto& [ptr, value] : leaves) {
            sbi_gen::HttpPayload p;
            p.iePath = ptr;
            p.ieValueLocation.value = "BODY";
            const bool secret = std::ranges::any_of(
                selection.body_pointers, [&](const std::string& sel) { return under(ptr, sel); });
            if (secret) {
                to_encrypt.push_back(std::move(value));
                p.value = enc_index(to_encrypt.size() - 1);
            } else {
                p.value = std::move(value);
            }
            payload.push_back(std::move(p));
        }
        block.payload = std::move(payload);
    }

    const std::string protected_b64 =
        base64url_encode(bytes(json{{"alg", "dir"}, {"enc", jwe_cipher}}.dump()));
    const std::string aad_b64 = base64url_encode(bytes(json(block).dump()));
    // RFC 7516 5.1 step 14: AAD = ASCII(BASE64URL(protected) || '.' || BASE64URL(aad)).
    const std::string aead_aad = protected_b64 + "." + aad_b64;
    std::string plaintext;
    if (!to_encrypt.empty()) {
        sbi_gen::DataToIntegrityProtectAndCipherBlock cipher_block;
        cipher_block.dataToEncrypt = std::move(to_encrypt);
        plaintext = json(cipher_block).dump();
    }

    // Nonce = IV salt (8 octets) || SEQ (32-bit, big-endian) -- TS 33.501 13.2.4.4.1.
    std::vector<std::uint8_t> iv = keys.iv_salt;
    for (int shift = 24; shift >= 0; shift -= 8) {
        iv.push_back(static_cast<std::uint8_t>((seq >> shift) & 0xFF));
    }
    std::unique_ptr<EVP_CIPHER_CTX, CtxFree> ctx(EVP_CIPHER_CTX_new());
    std::vector<std::uint8_t> ct(plaintext.size() + 16);
    std::vector<std::uint8_t> tag(16);
    int len = 0;
    int total = 0;
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, keys.key.data(), iv.data()) != 1 ||
        EVP_EncryptUpdate(ctx.get(),
                          nullptr,
                          &len,
                          reinterpret_cast<const unsigned char*>(aead_aad.data()),
                          static_cast<int>(aead_aad.size())) != 1 ||
        (!plaintext.empty() &&
         EVP_EncryptUpdate(ctx.get(),
                           ct.data(),
                           &len,
                           reinterpret_cast<const unsigned char*>(plaintext.data()),
                           static_cast<int>(plaintext.size())) != 1)) {
        return tl::unexpected("AES-GCM encryption failed");
    }
    total = plaintext.empty() ? 0 : len;
    if (EVP_EncryptFinal_ex(ctx.get(), ct.data() + total, &len) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, tag.data()) != 1) {
        return tl::unexpected("AES-GCM encryption failed");
    }
    total += len;
    ct.resize(static_cast<std::size_t>(total));

    sbi_gen::FlatJweJson jwe;
    jwe.protected_ = protected_b64;
    jwe.aad = aad_b64;
    jwe.iv = base64url_encode(iv);
    jwe.ciphertext = base64url_encode(ct);
    jwe.tag = base64url_encode(tag);
    return jwe;
}

std::optional<sbi_gen::MetaData> peek_metadata(const sbi_gen::FlatJweJson& jwe) {
    if (!jwe.aad) {
        return std::nullopt;
    }
    const auto raw = base64url_decode(*jwe.aad);
    if (!raw) {
        return std::nullopt;
    }
    const json block = json::parse(raw->begin(), raw->end(), nullptr, false);
    if (block.is_discarded() || !block.contains("metaData")) {
        return std::nullopt;
    }
    try {
        return block.at("metaData").get<sbi_gen::MetaData>();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

tl::expected<HttpMessage, OpenFailure>
open(const sbi_gen::FlatJweJson& jwe,
     const std::string& jwe_cipher,
     const JweKeys& keys,
     bool expect_response,
     const std::function<Selection(const HttpMessage&)>& selection_for) {
    const auto malformed = [](std::string d) {
        return tl::unexpected(OpenFailure{OpenError::Malformed, std::move(d)});
    };
    if (!jwe.protected_ || !jwe.aad || !jwe.iv || !jwe.tag) {
        return malformed("the JWE lacks protected / aad / iv / tag");
    }
    const auto header_raw = base64url_decode(*jwe.protected_);
    const json header =
        header_raw ? json::parse(header_raw->begin(), header_raw->end(), nullptr, false) : json();
    if (!header.is_object() || header.value("alg", "") != "dir" ||
        header.value("enc", "") != jwe_cipher) {
        return malformed("JWE protected header is not {alg: dir, enc: " + jwe_cipher + "}");
    }
    const auto iv = base64url_decode(*jwe.iv);
    const auto tag = base64url_decode(*jwe.tag);
    const auto ct = base64url_decode(jwe.ciphertext);
    const auto aad_raw = base64url_decode(*jwe.aad);
    if (!iv || iv->size() != 12 || !tag || tag->size() != 16 || !ct || !aad_raw) {
        return malformed("JWE iv / tag / ciphertext / aad do not decode");
    }
    const std::string aead_aad = *jwe.protected_ + "." + *jwe.aad;
    std::unique_ptr<EVP_CIPHER_CTX, CtxFree> ctx(EVP_CIPHER_CTX_new());
    std::vector<std::uint8_t> pt(ct->size() + 16);
    int len = 0;
    int total = 0;
    auto tag_copy = *tag;
    const bool ok =
        ctx && EVP_DecryptInit_ex(ctx.get(), gcm(jwe_cipher), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, 12, nullptr) == 1 &&
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, keys.key.data(), iv->data()) == 1 &&
        EVP_DecryptUpdate(ctx.get(),
                          nullptr,
                          &len,
                          reinterpret_cast<const unsigned char*>(aead_aad.data()),
                          static_cast<int>(aead_aad.size())) == 1 &&
        (ct->empty() ||
         (EVP_DecryptUpdate(ctx.get(), pt.data(), &len, ct->data(), static_cast<int>(ct->size())) ==
              1 &&
          (total = len, true))) &&
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, tag_copy.data()) == 1 &&
        EVP_DecryptFinal_ex(ctx.get(), pt.data() + total, &len) == 1;
    if (!ok) {
        return tl::unexpected(
            OpenFailure{OpenError::IntegrityCheckFailed, "JWE authentication tag check failed"});
    }
    pt.resize(static_cast<std::size_t>(total + len));

    sbi_gen::DataToIntegrityProtectBlock block;
    std::vector<json> secrets;
    try {
        block = json::parse(aad_raw->begin(), aad_raw->end())
                    .get<sbi_gen::DataToIntegrityProtectBlock>();
        if (!pt.empty()) {
            secrets = json::parse(pt.begin(), pt.end())
                          .get<sbi_gen::DataToIntegrityProtectAndCipherBlock>()
                          .dataToEncrypt;
        }
    } catch (const std::exception& e) {
        return malformed(std::string("reformatted blocks do not parse: ") + e.what());
    }
    const auto resolve = [&](const json& v, bool& was_secret) -> std::optional<json> {
        if (const auto idx = as_enc_index(v)) {
            was_secret = true;
            if (*idx >= secrets.size()) {
                return std::nullopt;
            }
            return std::optional<json>(std::in_place, secrets[*idx]);
        }
        was_secret = false;
        return std::optional<json>(std::in_place, v);
    };

    HttpMessage msg;
    std::vector<std::size_t> secret_segments;
    if (expect_response) {
        if (!block.statusLine) {
            return malformed("a response without statusLine");
        }
        try {
            msg.status = std::stoi(*block.statusLine);
        } catch (const std::exception&) {
            return malformed("statusLine is not a status code");
        }
    } else {
        if (!block.requestLine) {
            return malformed("a request without requestLine");
        }
        const auto& line = *block.requestLine;
        msg.method = line.method.value;
        msg.scheme = line.scheme.value;
        msg.authority = line.authority;
        auto segments = split(line.path, '/');
        for (std::size_t i = 0; i < segments.size(); ++i) {
            const json seg = json::parse(segments[i], nullptr, false);
            if (const auto idx = seg.is_discarded() ? std::nullopt : as_enc_index(seg)) {
                if (*idx >= secrets.size() || !secrets[*idx].is_string()) {
                    return malformed("encBlockIndex in the path out of range");
                }
                segments[i] = secrets[*idx].get<std::string>();
                secret_segments.push_back(i);
            }
        }
        std::string path;
        for (std::size_t i = 0; i < segments.size(); ++i) {
            path += (i == 0 ? "" : "/") + segments[i];
        }
        msg.path = path + (line.queryFragment ? "?" + *line.queryFragment : "");
    }
    std::set<std::string> clear_headers;
    if (block.headers) {
        for (const auto& h : *block.headers) {
            bool secret = false;
            const auto v = resolve(h.value, secret);
            if (!v || !v->is_string()) {
                return malformed("header " + h.header + " has no string value");
            }
            // SEC-11 (ADR-0530): header names are case-insensitive; the policy's are lower case.
            std::string name = h.header;
            std::ranges::transform(name, name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (!secret) {
                clear_headers.insert(name);
            }
            msg.headers.emplace_back(name, v->get<std::string>());
        }
    }
    std::vector<std::string> clear_pointers;
    if (block.payload) {
        json body;
        for (const auto& p : *block.payload) {
            bool secret = false;
            const auto v = resolve(p.value, secret);
            if (!v) {
                return malformed("encBlockIndex in the payload out of range");
            }
            if (!secret) {
                clear_pointers.push_back(p.iePath);
            }
            if (p.iePath.empty()) {
                body = *v;
            } else if (!ie_path_within_bounds(body, p.iePath)) {
                // SUS-03 (ADR-0530): an array index beyond the next free element would make
                // nlohmann allocate every element up to it ("/x/100000000").
                return malformed("iePath " + p.iePath + " skips array elements");
            } else {
                try {
                    body[json::json_pointer(p.iePath)] = *v;
                } catch (const std::exception&) {
                    return malformed("invalid iePath " + p.iePath);
                }
            }
        }
        msg.body = body.dump();
    }

    // TS 29.573 table 6.2.6.3-1 POLICY_MISMATCH: an IE the policy protects arrived in clear text.
    const Selection must = selection_for(msg);
    for (const auto& h : must.headers) {
        if (clear_headers.contains(h)) {
            return tl::unexpected(
                OpenFailure{OpenError::PolicyMismatch, "header " + h + " arrived unencrypted"});
        }
    }
    for (const auto& ptr : clear_pointers) {
        if (std::ranges::any_of(must.body_pointers,
                                [&](const std::string& sel) { return under(ptr, sel); })) {
            return tl::unexpected(
                OpenFailure{OpenError::PolicyMismatch, "IE " + ptr + " arrived unencrypted"});
        }
    }
    for (const auto i : must.path_segments) {
        if (std::ranges::find(secret_segments, i) == secret_segments.end()) {
            return tl::unexpected(OpenFailure{OpenError::PolicyMismatch,
                                              "a protected URI path segment arrived unencrypted"});
        }
    }
    return msg;
}

} // namespace sepp
