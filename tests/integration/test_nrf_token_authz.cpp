// ADR-0499 phase 2: the NRF authorizes an access token request before issuing (TS 33.501
// 13.4.1.1.2 step 2; TS 29.510 5.4.2.2.1 and Table 6.3.6.3-1).
#include "sbi_core/oauth2_client.hpp"

#include <map>
#include <string>
#include <vector>

#include "token_authz.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

const std::string kSmf = "9b2a0c1e-5d6f-4a7b-8c9d-0e1f2a3b4c5d";

json smf_profile() {
    return json{{"nfInstanceId", kSmf}, {"nfType", "SMF"}, {"nfStatus", "REGISTERED"}};
}

json pcf_profile(json services) {
    json p{{"nfInstanceId", "pcf-1"}, {"nfType", "PCF"}, {"nfStatus", "REGISTERED"}};
    if (!services.is_null()) {
        p["nfServiceList"] = std::move(services);
    }
    return p;
}

class NrfTokenAuthz : public ::testing::Test {
protected:
    nrf::TokenDecision ask(std::map<std::string, std::string> extra,
                           nrf::TokenRequester cert = {.cert_cn = "smf",
                                                       .cert_dns_names = {"smf"}}) {
        std::map<std::string, std::string> f{{"grant_type", "client_credentials"},
                                             {"nfInstanceId", kSmf},
                                             {"nfType", "SMF"},
                                             {"targetNfType", "PCF"},
                                             {"scope", "npcf-smpolicycontrol"}};
        for (auto& [k, v] : extra) {
            if (v.empty()) {
                f.erase(k);
            } else {
                f[k] = v;
            }
        }
        return nrf::authorize_token_request(
            f,
            cert,
            [&](const std::string& id) -> std::optional<json> {
                return registered.contains(id) ? std::optional{registered.at(id)} : std::nullopt;
            },
            [&](const std::string& t) {
                std::vector<json> out;
                for (const auto& [id, p] : registered) {
                    if (p.at("nfType") == t) {
                        out.push_back(p);
                    }
                }
                return out;
            });
    }
    std::map<std::string, json> registered{
        {kSmf, smf_profile()},
        {"pcf-1",
         pcf_profile(json{
             {"s1", {{"serviceInstanceId", "s1"}, {"serviceName", "npcf-smpolicycontrol"}}}})}};
};

TEST_F(NrfTokenAuthz, ARegisteredConsumerWithAMatchingCertificateGetsItsToken) {
    EXPECT_EQ(ask({}).error, "");
    // The NRF's own services need no registered producer.
    EXPECT_EQ(ask({{"targetNfType", "NRF"}, {"scope", "nnrf-disc"}}).error, "");
}

TEST_F(NrfTokenAuthz, MalformedRequestsAreInvalidRequestOrUnsupportedGrantType) {
    EXPECT_EQ(ask({{"grant_type", "password"}}).error, "unsupported_grant_type");
    EXPECT_EQ(ask({{"grant_type", ""}}).error, "invalid_request");
    EXPECT_EQ(ask({{"scope", ""}}).error, "invalid_request");
    EXPECT_EQ(ask({{"nfInstanceId", ""}}).error, "invalid_request");
    // The form checks alone (the switch off) refuse the same malformed requests.
    EXPECT_EQ(nrf::check_token_request_form({{"grant_type", "password"}}).error,
              "unsupported_grant_type");
}

TEST_F(NrfTokenAuthz, AnUnregisteredOrMisdescribedConsumerIsInvalidClient) {
    EXPECT_EQ(ask({{"nfInstanceId", "not-registered"}}).error, "invalid_client");
    EXPECT_EQ(ask({{"nfType", "UDM"}}).error, "invalid_client");
}

TEST_F(NrfTokenAuthz, ACertificateNotNamingTheConsumersNfTypeIsInvalidClient) {
    // A PCF certificate claiming the SMF's registered instance.
    EXPECT_EQ(ask({}, {.cert_cn = "pcf", .cert_dns_names = {"pcf", "localhost"}}).error,
              "invalid_client");
    EXPECT_EQ(ask({}, {.cert_cn = "", .cert_dns_names = {"localhost", "SMF"}}).error, "");
}

TEST_F(NrfTokenAuthz, AServiceNoProducerOffersToTheConsumerIsInvalidScope) {
    // Not offered by any registered PCF.
    EXPECT_EQ(ask({{"scope", "npcf-policyauthorization"}}).error, "invalid_scope");
    // Every plain service name in the scope must be authorized; resource scopes are not checked.
    EXPECT_EQ(ask({{"scope", "npcf-smpolicycontrol npcf-policyauthorization"}}).error,
              "invalid_scope");
    EXPECT_EQ(ask({{"scope", "npcf-smpolicycontrol npcf-smpolicycontrol:x:y"}}).error, "");
    // No registered producer of the target type at all.
    EXPECT_EQ(ask({{"targetNfType", "UDM"}, {"scope", "nudm-sdm"}}).error, "invalid_scope");
}

TEST_F(NrfTokenAuthz, AllowedNfTypesOnTheServiceOrProfileRestrictTheConsumer) {
    registered["pcf-1"]["nfServiceList"]["s1"]["allowedNfTypes"] = json::array({"AMF"});
    EXPECT_EQ(ask({}).error, "invalid_scope");
    registered["pcf-1"]["nfServiceList"]["s1"]["allowedNfTypes"] = json::array({"AMF", "SMF"});
    EXPECT_EQ(ask({}).error, "");
    // A profile listing no services: its profile-level allowedNfTypes decide.
    registered["pcf-1"] = pcf_profile(nullptr);
    registered["pcf-1"]["allowedNfTypes"] = json::array({"AMF"});
    EXPECT_EQ(ask({}).error, "invalid_scope");
    registered["pcf-1"].erase("allowedNfTypes");
    EXPECT_EQ(ask({}).error, "");
}

TEST_F(NrfTokenAuthz, TheDeprecatedNfServicesArrayIsReadToo) {
    registered["pcf-1"] = pcf_profile(nullptr);
    registered["pcf-1"]["nfServices"] =
        json::array({{{"serviceInstanceId", "s1"}, {"serviceName", "npcf-smpolicycontrol"}}});
    EXPECT_EQ(ask({}).error, "");
    EXPECT_EQ(ask({{"scope", "npcf-am-policy-control"}}).error, "invalid_scope");
}

TEST_F(NrfTokenAuthz, TheErrorBodyIsAnAccessTokenErr) {
    const auto body = nrf::access_token_err(ask({{"nfType", "UDM"}}));
    EXPECT_EQ(body.at("error"), "invalid_client");
    EXPECT_TRUE(body.at("error_description").is_string());
}

TEST_F(NrfTokenAuthz, EveryRefusalIsATs29510AccessTokenErrValue) {
    const std::vector<nrf::TokenDecision> refusals{
        ask({{"grant_type", "password"}}),
        ask({{"scope", ""}}),
        ask({{"nfInstanceId", "not-registered"}}),
        ask({}, {.cert_cn = "pcf", .cert_dns_names = {"pcf"}}),
        ask({{"scope", "npcf-policyauthorization"}}),
        ask({{"targetNfType", "UDM"}, {"scope", "nudm-sdm"}}),
    };
    for (const auto& d : refusals) {
        EXPECT_TRUE(sbi_core::is_access_token_err_code(d.error)) << d.error;
    }
}

TEST_F(NrfTokenAuthz, TheConsumerShowsTheRefusalsErrorAndDescription) {
    // No UDM registered: the NRF refuses with invalid_scope (TS 33.501 13.4.1.1.2); the consumer
    // names that value and the NRF's description rather than dumping the raw body.
    const auto d = ask({{"targetNfType", "UDM"}, {"scope", "nudm-sdm"}});
    const auto shown = sbi_core::describe_token_refusal(400, nrf::access_token_err(d).dump());
    EXPECT_EQ(shown, "AccessTokenErr invalid_scope: " + d.description);
    EXPECT_EQ(sbi_core::describe_token_refusal(
                  401, R"({"error":"invalid_client","error_uri":"https://nrf/e"})"),
              "AccessTokenErr invalid_client (https://nrf/e)");
}

TEST_F(NrfTokenAuthz, TheConsumerFlagsANonConformantRefusal) {
    EXPECT_EQ(sbi_core::describe_token_refusal(400, R"({"error":"access_denied"})"),
              "AccessTokenErr access_denied (not a TS 29.510 AccessTokenErr value)");
    EXPECT_EQ(sbi_core::describe_token_refusal(400, "nope"),
              "non-conformant token refusal, no AccessTokenErr body (TS 29.510 Table "
              "6.3.4.2.2-2): status 400: nope");
    EXPECT_EQ(sbi_core::describe_token_refusal(500, "x"), "status 500: x");
}

TEST_F(NrfTokenAuthz, TheConsumerShowsA403ProblemDetails) {
    const json pd{{"status", 403},
                  {"cause", "MISSING_PARAMETER"},
                  {"detail", "requester PLMN missing"},
                  {"invalidParams", json::array({{{"param", "/requesterPlmn"}}})}};
    EXPECT_EQ(sbi_core::describe_token_refusal(403, pd.dump()),
              "ProblemDetails 403 MISSING_PARAMETER: requester PLMN missing "
              "[invalidParams: /requesterPlmn]");
}

} // namespace
