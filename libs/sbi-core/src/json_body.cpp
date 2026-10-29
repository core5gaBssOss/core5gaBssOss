#include "sbi_core/json_body.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace sbi_core::http2 {

namespace {

Response problem_with_cause(int status,
                            const std::string& title,
                            const std::string& detail,
                            const std::string& cause) {
    nlohmann::json j = sbi_core::make_problem_details(status, title, detail, cause);
    Response r;
    r.status = status;
    r.headers.emplace("content-type", "application/problem+json");
    r.body = j.dump();
    return r;
}

// The media type of a Content-Type value, lower-cased, without parameters.
std::string media_type(const Request& req) {
    const auto it = req.headers.find("content-type");
    if (it == req.headers.end()) {
        return {};
    }
    std::string type = it->second.substr(0, it->second.find(';'));
    while (!type.empty() && (type.back() == ' ' || type.back() == '\t')) {
        type.pop_back();
    }
    std::ranges::transform(
        type, type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return type;
}

} // namespace

tl::expected<nlohmann::json, Response> apply_json_patch_body(const Request& req,
                                                             const nlohmann::json& target) {
    constexpr const char* kJsonPatch = "application/json-patch+json";
    if (media_type(req) != kJsonPatch) {
        auto r = problem_response(415,
                                  "Unsupported Media Type",
                                  std::string("this resource is patched with ") + kJsonPatch,
                                  std::nullopt); // Table 5.2.7.2-1 defines no 415 cause
        r.headers.emplace("accept-patch", kJsonPatch);
        return tl::unexpected(std::move(r));
    }
    nlohmann::json ops;
    try {
        ops = nlohmann::json::parse(req.body);
    } catch (const nlohmann::json::parse_error& e) {
        return tl::unexpected(
            problem_with_cause(400, "Malformed JSON", e.what(), "INVALID_MSG_FORMAT"));
    }
    if (json_has_duplicate_keys(req.body)) {
        return tl::unexpected(problem_with_cause(
            400, "Bad Request", "a JSON object repeats a member name", "INVALID_MSG_FORMAT"));
    }
    if (!ops.is_array() || ops.empty()) {
        return tl::unexpected(problem_with_cause(400,
                                                 "Bad Request",
                                                 "a JSON Patch body is a non-empty array of "
                                                 "PatchItem (RFC 6902)",
                                                 "INVALID_MSG_FORMAT"));
    }
    try {
        return target.patch(ops);
    } catch (const nlohmann::json::exception& e) {
        return tl::unexpected(
            problem_with_cause(400, "Bad Request", e.what(), "INVALID_MSG_FORMAT"));
    }
}

Response problem_response(int status,
                          const std::string& title,
                          const std::string& detail,
                          std::optional<std::string> cause) {
    auto pd = sbi_core::make_problem_details(status, title, detail, std::move(cause));
    nlohmann::json j = pd;
    Response r;
    r.status = status;
    r.headers.emplace("content-type", "application/problem+json");
    r.body = j.dump();
    return r;
}

const char* dto_error_cause(const nlohmann::json::exception& e) {
    return dynamic_cast<const nlohmann::json::out_of_range*>(&e) != nullptr
               ? "MANDATORY_IE_MISSING"
               : "MANDATORY_IE_INCORRECT";
}

namespace {

// SAX handler: one name set per open object; a repeated name sets `duplicate` and stops the parse.
struct DuplicateKeyDetector : nlohmann::json::json_sax_t {
    std::vector<std::set<std::string>> objects;
    bool duplicate = false;

    bool null() override { return true; }
    bool boolean(bool) override { return true; }
    bool number_integer(number_integer_t) override { return true; }
    bool number_unsigned(number_unsigned_t) override { return true; }
    bool number_float(number_float_t, const string_t&) override { return true; }
    bool string(string_t&) override { return true; }
    bool binary(binary_t&) override { return true; }
    bool start_object(std::size_t) override {
        objects.emplace_back();
        return true;
    }
    bool key(string_t& name) override {
        if (!objects.back().insert(name).second) {
            duplicate = true;
            return false; // stop
        }
        return true;
    }
    bool end_object() override {
        objects.pop_back();
        return true;
    }
    bool start_array(std::size_t) override { return true; }
    bool end_array() override { return true; }
    bool parse_error(std::size_t, const std::string&, const nlohmann::json::exception&) override {
        return false;
    }
};

} // namespace

bool json_has_duplicate_keys(std::string_view body) {
    DuplicateKeyDetector detector;
    const std::string text(body);
    nlohmann::json::sax_parse(text, &detector);
    return detector.duplicate;
}

} // namespace sbi_core::http2
