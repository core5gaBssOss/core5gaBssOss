// ADR-0605 (ADR-0570 class, AMF part): the URIs the AMF builds from a SUPI encode it as one path
// segment. The builders are the AMF's own (nfs/amf/src/peer_uris.hpp, the functions ngap_task.cpp
// calls), and the peer is the real SMSF.
//
// Why not the AMF binary end to end: the SUPI would have to come from a RegistrationRequest whose
// MSIN carries a nibble 0xA..0xE (the AMF's NAS decoder writes '?' for it), pass 5G AKA (AUSF and
// UDM with such a subscriber provisioned) and only then reach the SMSF. The test UE builder
// (ue_nas_driver) encodes decimal digits only, and the AUSF still pasted supiOrSuci into the UDM
// URI unencoded until ADR-0603. So this test calls the AMF's builder and sends what the AMF sends.

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <thread>

#include "peer_uris.hpp"
#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

namespace {

constexpr const char* kSmsfBase = "https://127.0.0.1:7793";

sbi_core::http2::TlsConfig test_tls() {
    return sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
    };
}

// SMSServiceParameterUpdate with an empty JSON Patch: 204 iff the SMSF holds a UE SMS context for
// `supi`, 404 otherwise -- the SMSF's own state.
int smsf_context_status(sbi_core::http2::Client& client, const std::string& supi) {
    sbi_core::http2::ClientRequest patch;
    patch.method = "PATCH";
    patch.url = std::string(kSmsfBase) + "/nsmsf-sms/v2/ue-contexts/" +
                sbi_core::uri::encode_path_segment(supi);
    patch.headers.emplace("content-type", "application/json-patch+json");
    patch.body = "[]";
    const auto resp = client.send(patch);
    return resp ? static_cast<int>(resp->status) : -1;
}

} // namespace

// A SUPI that ends in '?' (what the AMF's NAS decoder makes of a trailing 0xA nibble). Pasted into
// `PUT /nsmsf-sms/v2/ue-contexts/{supi}` unencoded, the '?' ends the path, so the SMSF activates
// SMS over NAS for the subscriber named before it -- another UE's context, holding this AMF's id.
// Encoded, the SMSF stores the context under the whole SUPI and the other subscriber has none.
TEST(AmfPeerUriEncoding, SupiWithQueryCharacterCannotActivateSmsForAnotherSubscriber) {
    const std::string victim = "imsi-999700000000605";
    const std::string supi = victim + "?";

    // The builders' shape: one segment, and the SMSF's sendsms / the AMF's callback route keep
    // their own segments after it.
    EXPECT_EQ(amf::peer_uris::smsf_ue_context(kSmsfBase, supi),
              std::string(kSmsfBase) + "/nsmsf-sms/v2/ue-contexts/imsi-999700000000605%3F");
    EXPECT_EQ(amf::peer_uris::smsf_send_sms(kSmsfBase, "nai-a/b?c"),
              std::string(kSmsfBase) + "/nsmsf-sms/v2/ue-contexts/nai-a%2Fb%3Fc/sendsms");
    EXPECT_EQ(amf::peer_uris::sm_context_status_callback("https://amf", "nai-a/5?x", 1),
              "https://amf/namf-callback/v1/sm-context-status/nai-a%2F5%3Fx/1");
    EXPECT_EQ(amf::peer_uris::smsf_ue_context(kSmsfBase, "imsi-999700000000001"),
              std::string(kSmsfBase) + "/nsmsf-sms/v2/ue-contexts/imsi-999700000000001");

    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    nf_test::SpawnedProcess smsf(SMSF_PATH);
    ASSERT_GT(smsf.pid(), 0) << "failed to fork smsf";
    sbi_core::http2::Client client(test_tls());
    nf_test::attach_test_tokens(client);

    bool reachable = false;
    for (int attempt = 0; attempt < 200 && !reachable; ++attempt) {
        reachable = smsf_context_status(client, victim) == 404;
        if (!reachable) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    ASSERT_TRUE(reachable) << "smsf never answered 404 for the unactivated subscriber";

    // The request activate_sms_at_smsf() sends (ngap_task.cpp), URL from the AMF's builder.
    sbi_core::http2::ClientRequest put;
    put.method = "PUT";
    put.url = amf::peer_uris::smsf_ue_context(kSmsfBase, supi);
    put.headers.emplace("content-type", "application/json");
    put.body = nlohmann::json{{"supi", supi},
                              {"amfId", "00000000-0000-4000-8000-0000000000a1"},
                              {"accessType", "3GPP_ACCESS"}}
                   .dump();
    const auto resp = client.send(put);
    ASSERT_TRUE(resp.has_value());
    EXPECT_EQ(resp->status, 201) << resp->body;

    EXPECT_EQ(smsf_context_status(client, victim), 404)
        << "the AMF's Activate created an SMS context for another subscriber";
    EXPECT_EQ(smsf_context_status(client, supi), 204) << "no SMS context under the whole SUPI";
}
