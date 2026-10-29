#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <tl/expected.hpp>
#include <utility>
#include <vector>

#include "TS29573_JOSEProtectedMessageForwarding.hpp"
#include "n32.hpp"

// PRINS message protection on N32-f (TS 33.501 13.2.4, TS 29.573 5.3.2 / 6.2.5; ADR-0494): the
// reformatting of an HTTP message into DataToIntegrityProtectBlock +
// DataToIntegrityProtectAndCipher Block, JWE (RFC 7516, "dir" + A128GCM / A256GCM, Flattened JSON
// Serialization), and the reverse at the receiving SEPP. No I/O. Private to nfs/sepp.

namespace sepp {

struct HttpMessage {
    // Request: method, scheme, authority, path (with "?query" when present). Response: status.
    std::string method;
    std::string scheme = "https";
    std::string authority;
    std::string path;
    int status = 0;
    std::vector<std::pair<std::string, std::string>> headers; // lower-case names
    std::string body;                                         // JSON or empty
    bool is_response() const { return status != 0; }
};

// TS 33.501 13.2.4.4.1: the session key / IV salt pair for one direction of one HTTP session.
enum class KeyUse { ParallelRequest, ParallelResponse, ReverseRequest, ReverseResponse };

struct JweKeys {
    std::vector<std::uint8_t> key;     // 16 (A128GCM) or 32 (A256GCM) octets
    std::vector<std::uint8_t> iv_salt; // 8 octets
};

// N32-KDF with the N32-f context id the message carries in its metadata (TS 29.573 6.2.5.2.9: the
// receiving SEPP's id) -- the one value both SEPPs see on every message (ADR-0494).
JweKeys derive_keys(const std::vector<std::uint8_t>& master_key,
                    const std::string& n32f_context_id,
                    KeyUse use,
                    const std::string& jwe_cipher);

// Which key a message uses: requests from the N32-c initiator travel in the "parallel" session.
KeyUse key_use(bool sender_is_initiator, bool is_response);

// What the protection policy (plus TS 33.501 5.9.3.3's always-encrypted types) says to encrypt in
// a message of the API (method, path without query): body JSON pointers, header names, and path
// segment indices.
struct Selection {
    std::vector<std::string> body_pointers;
    std::set<std::string> headers;
    std::set<std::size_t> path_segments;
};
Selection select(const std::optional<sbi_gen::ProtectionPolicy>& policy,
                 const std::string& method,
                 const std::string& path_without_query,
                 bool response);

std::string base64url_encode(const std::vector<std::uint8_t>& in);
std::optional<std::vector<std::uint8_t>> base64url_decode(const std::string& in);

// Sending SEPP: reformat + JWE. `seq` is the per-IV-salt counter (13.2.4.4.1).
tl::expected<sbi_gen::FlatJweJson, std::string> protect(const HttpMessage& message,
                                                        const Selection& selection,
                                                        const std::string& jwe_cipher,
                                                        const JweKeys& keys,
                                                        std::uint32_t seq,
                                                        const std::string& n32f_context_id,
                                                        const std::string& message_id);

// Receiving SEPP, step 1: the metadata (integrity-protected, not yet verified) naming the context.
std::optional<sbi_gen::MetaData> peek_metadata(const sbi_gen::FlatJweJson& jwe);

// Why opening failed -- each maps to a TS 29.573 cause.
enum class OpenError { Malformed, IntegrityCheckFailed, PolicyMismatch };
struct OpenFailure {
    OpenError error;
    std::string detail;
};

// Receiving SEPP, step 2: verify + decrypt + reconstruct. `selection` is what the policy says
// must have been encrypted: such an IE arriving in clear text is POLICY_MISMATCH.
tl::expected<HttpMessage, OpenFailure>
open(const sbi_gen::FlatJweJson& jwe,
     const std::string& jwe_cipher,
     const JweKeys& keys,
     bool expect_response,
     const std::function<Selection(const HttpMessage&)>& selection_for);

// SUS-03 (ADR-0530): every array index in the JSON pointer `ptr` addresses an element of `body`
// that exists or the next free one, so a reconstructed body cannot be made to allocate a huge
// array.
bool ie_path_within_bounds(const nlohmann::json& body, const std::string& ptr);

} // namespace sepp
