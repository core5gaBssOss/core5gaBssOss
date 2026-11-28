// SBI-03 invalidParams, step 2, SMF (ADR-0621; sbi-core step 1 is ADR-0607). TS 29.500 V19.7.0
// Table 5.2.7.2-1 NOTE 1: "'invalidParams' attribute shall be included in the 'ProblemDetails'
// data structure indicating unsupported, missing or incorrect IE(s) or query parameter(s) or
// 3gpp-Sbi-Discovery-* header(s)." InvalidParam.param is a JSON Pointer for a body attribute
// (TS29571_CommonData.yaml). Probes, one per kind of SMF site:
//   - CreateSMContext without members this build requires (supi, dnn absent): one entry each;
//   - UpdateSMContext whose n2SmInfo binary part does not decode as the transfer its
//     n2SmInfoType declares: `/n2SmInfo`, for four of the six n2SmInfo sites that share
//     n2_sm_info_not_decodable (PDU_RES_SETUP_RSP, PDU_RES_NTY, UE_CONTEXT_RESUME_REQ,
//     PATH_SWITCH_REQ). The four bytes 0xFF are what test_smf_n2sminfo_dispatch.cpp already
//     uses as a malformed transfer; aper_decode_complete refuses them for all four types.
// Real nrf + upf + smf + pcf processes over TLS 1.3 + mTLS HTTP/2 with a real NRF token.
#include "sbi_core/http2_client.hpp"
#include "sbi_core/multipart.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {
using nlohmann::json;
constexpr const char* kSmContexts = "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts";

sbi_core::http2::Client make_client() {
    sbi_core::http2::TlsConfig tls{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
    return sbi_core::http2::Client(std::move(tls));
}

std::string fetch_token() {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body =
        "grant_type=client_credentials&nfInstanceId=" + std::string(nf_test::kTestAfInstanceId) +
        "&scope=nsmf-pdusession&targetNfType=SMF";
    auto resp = nf_test::send_token_request(req);
    for (int attempt = 0; attempt < 50 && (!resp.has_value() || resp->status != 200); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // the SMF's NFRegister
        resp = nf_test::send_token_request(req);
    }
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

nf_test::SpawnedProcess spawn_after_nrf(const char* path) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return nf_test::SpawnedProcess(path);
}

std::vector<std::string> params_of(const json& problem) {
    std::vector<std::string> out;
    for (const auto& p : problem.value("invalidParams", json::array())) {
        out.push_back(p.at("param").get<std::string>());
    }
    return out;
}

// SmContextCreateData (TS29502_Nsmf_PDUSession.yaml) with its four required members
// (servingNfId, servingNetwork, anType, smContextStatusUri) plus `extra`.
std::string create_json(const json& extra) {
    json data{
        {"servingNfId", "00000000-0000-4000-8000-0000000000aa"},
        {"servingNetwork", json{{"mcc", "999"}, {"mnc", "70"}}},
        {"anType", "3GPP_ACCESS"},
        {"smContextStatusUri", "https://example.com/sm-status"},
    };
    data.update(extra);
    return data.dump();
}

class SmfInvalidParams : public ::testing::Test {
protected:
    nf_test::SpawnedProcess nrf_{NRF_PATH};
    nf_test::SpawnedProcess upf_ = spawn_after_nrf(UPF_PATH);
    nf_test::SpawnedProcess smf_{SMF_PATH};
    nf_test::SpawnedProcess pcf_{PCF_PATH};
    sbi_core::http2::Client client_ = make_client();
    std::string token_;

    void SetUp() override {
        nf_test::wait_tcp_listening(7777);
        nf_test::wait_tcp_listening(7779);
        token_ = fetch_token();
        ASSERT_FALSE(token_.empty()) << "failed to obtain OAuth2 token from nrf";
    }

    std::optional<sbi_core::http2::ClientResponse> post_create(const std::string& json_part_body) {
        sbi_core::multipart::Part part;
        part.content_type = "application/json";
        part.body = json_part_body;
        const auto encoded = sbi_core::multipart::encode({part});
        sbi_core::http2::ClientRequest req;
        req.method = "POST";
        req.url = kSmContexts;
        req.headers.emplace("content-type", encoded.content_type_header);
        req.headers.emplace("authorization", "Bearer " + token_);
        req.body = encoded.body;
        auto resp = client_.send(req);
        if (!resp.has_value()) {
            return std::nullopt;
        }
        return *resp;
    }

    // An SM context to update. Retries while the SMF is still discovering its peers.
    std::string create_context() {
        for (int attempt = 0; attempt < 40; ++attempt) {
            auto resp = post_create(create_json({{"supi", "imsi-999700000000621"},
                                                 {"pduSessionId", 30 + attempt},
                                                 {"dnn", "internet"},
                                                 {"sNssai", json{{"sst", 1}}}}));
            if (resp.has_value() && resp->status == 201) {
                const auto location = resp->headers.find("location");
                if (location == resp->headers.end()) {
                    return "";
                }
                return location->second.substr(location->second.rfind('/') + 1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        return "";
    }

    // UpdateSMContext with `n2SmInfoType` and a binary part that is not a PER-encoded transfer;
    // returns the 400 ProblemDetails after checking status and cause.
    json expect_undecodable_n2(const std::string& ref, const std::string& n2_sm_info_type) {
        sbi_core::multipart::Part json_part;
        json_part.content_type = "application/json";
        json_part.body =
            json{{"n2SmInfoType", n2_sm_info_type}, {"n2SmInfo", json{{"contentId", "n2SmInfo"}}}}
                .dump();
        sbi_core::multipart::Part bin_part;
        bin_part.content_type = "application/vnd.3gpp.ngap";
        bin_part.content_id = "n2SmInfo";
        bin_part.body = std::string(4, '\xFF');
        const auto encoded = sbi_core::multipart::encode({json_part, bin_part});
        sbi_core::http2::ClientRequest req;
        req.method = "POST";
        req.url = std::string(kSmContexts) + "/" + ref + "/modify";
        req.headers.emplace("content-type", encoded.content_type_header);
        req.headers.emplace("authorization", "Bearer " + token_);
        req.body = encoded.body;
        auto resp = client_.send(req);
        EXPECT_TRUE(resp.has_value()) << n2_sm_info_type;
        if (!resp.has_value()) {
            return json::object();
        }
        EXPECT_EQ(resp->status, 400) << n2_sm_info_type << ": " << resp->body;
        const auto problem = json::parse(resp->body);
        EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_INCORRECT") << n2_sm_info_type;
        return problem;
    }
};

TEST_F(SmfInvalidParams, CreateWithoutSupiAndDnnNamesBothBodyMembers) {
    // pduSessionId and sNssai present: only the two absent members are named, in check order.
    auto resp = post_create(create_json({{"pduSessionId", 5}, {"sNssai", json{{"sst", 1}}}}));
    ASSERT_TRUE(resp.has_value());
    ASSERT_EQ(resp->status, 400) << resp->body;
    const auto problem = json::parse(resp->body);
    EXPECT_EQ(problem.value("cause", ""), "MANDATORY_IE_MISSING");
    EXPECT_EQ(params_of(problem), (std::vector<std::string>{"/supi", "/dnn"})) << problem.dump();
}

TEST_F(SmfInvalidParams, UndecodableN2SmInfoNamesTheN2SmInfoMember) {
    const std::string ref = create_context();
    ASSERT_FALSE(ref.empty()) << "SMF never created an SM context";
    for (const char* type :
         {"PDU_RES_SETUP_RSP", "PDU_RES_NTY", "UE_CONTEXT_RESUME_REQ", "PATH_SWITCH_REQ"}) {
        const auto problem = expect_undecodable_n2(ref, type);
        EXPECT_EQ(params_of(problem), std::vector<std::string>{"/n2SmInfo"})
            << type << ": " << problem.dump();
    }
}

} // namespace
