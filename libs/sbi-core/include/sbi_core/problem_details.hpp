#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Fields transcribed verbatim from TS29571_CommonData.yaml, components.schemas.ProblemDetails
// and .InvalidParam (3GPP R19 OpenAPI, branch REL-19, commit
// bca84b60a37773133bcae97e5c6c0d10a93b47b6, lines 518-559 and 615-630). See docs/DECISIONS.md for
// why this type is hand-written here instead of coming from tools/sbi-codegen: sbi-core is a Phase
// 0 dependency of the codegen pipeline itself, so ProblemDetails/InvalidParam are the one
// deliberate, disclosed exception to "never hand-write a DTO." Phase 1 generated code should reuse
// these types rather than regenerating them.
//
// accessTokenError, accessTokenRequest, and noProfileMatchInfo reference schemas defined in
// TS29510_Nnrf_NFAccessToken.yaml / TS29510_Nnrf_NFDiscovery.yaml. Modeling those fully would pull
// in the NRF access-token and discovery schemas before Phase 1 exists, so for now they are passed
// through as opaque JSON. This is a disclosed simplification, not a fabricated shape.

namespace sbi_core {

struct InvalidParam {
    std::string param;
    std::optional<std::string> reason;
};

void to_json(nlohmann::json& j, const InvalidParam& v);
void from_json(const nlohmann::json& j, InvalidParam& v);

struct ProblemDetails {
    std::optional<std::string> type;
    std::optional<std::string> title;
    std::optional<std::int32_t> status;
    std::optional<std::string> detail;
    std::optional<std::string> instance;
    std::optional<std::string> cause;
    std::optional<std::vector<InvalidParam>> invalid_params;
    std::optional<std::string> supported_features;
    std::optional<nlohmann::json> access_token_error;   // opaque, see file header
    std::optional<nlohmann::json> access_token_request; // opaque, see file header
    std::optional<std::string> nrf_id;
    std::optional<std::vector<std::string>> supported_api_versions;
    std::optional<nlohmann::json> no_profile_match_info; // opaque, see file header
};

void to_json(nlohmann::json& j, const ProblemDetails& v);
void from_json(const nlohmann::json& j, ProblemDetails& v);

// Convenience builders for the cases sbi-core itself needs (TS 29.500 clause 5.2.7 generic errors).
ProblemDetails make_problem_details(std::int32_t status,
                                    std::string title,
                                    std::string detail,
                                    std::optional<std::string> cause = std::nullopt);

// Same, with an invalidParams array (ADR-0607). An empty `invalid_params` leaves the attribute
// out: the YAML gives invalidParams minItems 1, so an empty array would not conform.
ProblemDetails make_problem_details(std::int32_t status,
                                    std::string title,
                                    std::string detail,
                                    std::optional<std::string> cause,
                                    std::vector<InvalidParam> invalid_params);

// Builders for InvalidParam.param in the four encodings its YAML description defines
// (TS29571_CommonData.yaml, components.schemas.InvalidParam.properties.param, ADR-0607):
//  - an attribute in a JSON body: the attribute's name encoded as a JSON Pointer (RFC 6901). Taking
//    a json_pointer, not a string, makes the escaping of '~' and '/' the library's, not the
//    caller's;
//  - an HTTP header: "header " plus the header's name;
//  - a query parameter: "query " plus the query parameter's name;
//  - a variable part of a resource URI path: the variable's name including "{" and "}" as the
//    OpenAPI notation writes it. `name` is given without the braces, e.g. "supi" -> "{supi}".
InvalidParam invalid_body_ie(const nlohmann::json::json_pointer& pointer,
                             std::optional<std::string> reason = std::nullopt);
InvalidParam invalid_header(std::string_view name,
                            std::optional<std::string> reason = std::nullopt);
InvalidParam invalid_query_param(std::string_view name,
                                 std::optional<std::string> reason = std::nullopt);
InvalidParam invalid_path_variable(std::string_view name,
                                   std::optional<std::string> reason = std::nullopt);

// True for the causes TS 29.500 Table 5.2.7.2-1 marks with NOTE 1 (V19.7.0: "'invalidParams'
// attribute shall be included in the 'ProblemDetails' data structure indicating unsupported,
// missing or incorrect IE(s) or query parameter(s) or 3gpp-Sbi-Discovery-* header(s)."):
// INVALID_QUERY_PARAM, MANDATORY_QUERY_PARAM_INCORRECT, OPTIONAL_QUERY_PARAM_INCORRECT,
// MANDATORY_QUERY_PARAM_MISSING, MANDATORY_IE_INCORRECT, OPTIONAL_IE_INCORRECT,
// MANDATORY_IE_MISSING and MISSING_PARAMETER.
bool table_5272_note1_applies(std::string_view cause);

} // namespace sbi_core
