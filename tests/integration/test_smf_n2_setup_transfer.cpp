// ADR-0642: the SMF's PDU Session Resource Setup Request Transfer (TS 38.413 V19.4.0 9.3.4.1).
//
// - SmfN2SetupTransfer.EncodesTheMandatoryIesAndTheGivenQosFlow: the shared builder
//   (nfs/smf/src/n2_sm_info.cpp) decodes back to the UL NG-U UP TNL Information, PDU Session Type,
//   the one QoS Flow Setup Request Item and the PDU Session Aggregate Maximum Bit Rate it was
//   given.
// - SmfN2SetupTransfer.EstablishmentN1N2CarriesN2SmInfoMatchingTheHandoverTransfer: TS 23.502
//   4.3.2.2.1 step 11 -- the CreateSMContext's N1N2MessageTransfer carries N2 SM information
//   (TS29518 N2InfoContainer, ngapIeType PDU_RES_SETUP_REQ) next to the N1 SM container, its UL
//   F-TEID is the UPF's, and the handover transfer (HANDOVER_REQUIRED) later describes the same
//   QoS flow and F-TEID.
//
// The second test spawns the real NRF, UPF, SMF and PCF; the AMF (7778) is an in-process stand-in
// that records each N1N2MessageTransfer.

#include "sbi_core/http2_client.hpp"
#include "sbi_core/http2_server.hpp"
#include "sbi_core/multipart.hpp"

#include <boost/asio/io_context.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "n2_sm_info.hpp"
#include "ngap_core/ngap_codec.hpp"
#include "spawn_guard.hpp"
#include "test_tokens.hpp"

#include <gtest/gtest.h>

extern "C" {
#include <GTPTunnel.h>
#include <NonDynamic5QIDescriptor.h>
#include <PDUSessionAggregateMaximumBitRate.h>
#include <PDUSessionResourceSetupRequestTransfer.h>
#include <PDUSessionType.h>
#include <QosFlowSetupRequestItem.h>
#include <QosFlowSetupRequestList.h>
#include <UPTransportLayerInformation.h>
}

namespace {

using nlohmann::json;
using namespace std::chrono_literals;

// What a decoded transfer says, in plain values.
struct DecodedTransfer {
    std::uint32_t ul_teid = 0;
    std::array<std::uint8_t, 4> ul_ipv4{};
    long pdu_session_type = -1;
    std::size_t qos_flow_count = 0;
    long qfi = -1;
    long five_qi = -1;
    long arp_priority_level = -1;
    long pre_emption_capability = -1;
    long pre_emption_vulnerability = -1;
    std::optional<unsigned long> ambr_dl_bps;
    std::optional<unsigned long> ambr_ul_bps;
};

std::optional<DecodedTransfer> decode_transfer(const std::vector<std::uint8_t>& bytes) {
    auto* transfer = static_cast<PDUSessionResourceSetupRequestTransfer_t*>(
        ::ngap::decode_value(&asn_DEF_PDUSessionResourceSetupRequestTransfer, bytes));
    if (transfer == nullptr) {
        return std::nullopt;
    }
    DecodedTransfer out;
    bool ok = true;

    if (const auto* ie = ::ngap::find_ie(transfer->protocolIEs, 139); ie != nullptr) {
        auto* tnl = static_cast<UPTransportLayerInformation_t*>(
            ::ngap::decode_ie_value(&asn_DEF_UPTransportLayerInformation, *ie));
        if (tnl != nullptr && tnl->present == UPTransportLayerInformation_PR_gTPTunnel &&
            tnl->choice.gTPTunnel->gTP_TEID.size == 4 &&
            tnl->choice.gTPTunnel->transportLayerAddress.size == 4) {
            const auto* t = tnl->choice.gTPTunnel->gTP_TEID.buf;
            out.ul_teid = (static_cast<std::uint32_t>(t[0]) << 24) |
                          (static_cast<std::uint32_t>(t[1]) << 16) |
                          (static_cast<std::uint32_t>(t[2]) << 8) | t[3];
            std::copy_n(tnl->choice.gTPTunnel->transportLayerAddress.buf, 4, out.ul_ipv4.begin());
        } else {
            ok = false;
        }
        if (tnl != nullptr) {
            ASN_STRUCT_FREE(asn_DEF_UPTransportLayerInformation, tnl);
        }
    } else {
        ok = false;
    }

    if (const auto* ie = ::ngap::find_ie(transfer->protocolIEs, 134); ie != nullptr) {
        auto* type =
            static_cast<PDUSessionType_t*>(::ngap::decode_ie_value(&asn_DEF_PDUSessionType, *ie));
        if (type != nullptr) {
            out.pdu_session_type = *type;
            ASN_STRUCT_FREE(asn_DEF_PDUSessionType, type);
        }
    }

    if (const auto* ie = ::ngap::find_ie(transfer->protocolIEs, 136); ie != nullptr) {
        auto* list = static_cast<QosFlowSetupRequestList_t*>(
            ::ngap::decode_ie_value(&asn_DEF_QosFlowSetupRequestList, *ie));
        if (list != nullptr) {
            out.qos_flow_count = static_cast<std::size_t>(list->list.count);
            if (list->list.count > 0) {
                const auto* item = list->list.array[0];
                out.qfi = item->qosFlowIdentifier;
                const auto& params = item->qosFlowLevelQosParameters;
                if (params.qosCharacteristics.present == QosCharacteristics_PR_nonDynamic5QI) {
                    out.five_qi = params.qosCharacteristics.choice.nonDynamic5QI->fiveQI;
                }
                out.arp_priority_level = params.allocationAndRetentionPriority.priorityLevelARP;
                out.pre_emption_capability =
                    params.allocationAndRetentionPriority.pre_emptionCapability;
                out.pre_emption_vulnerability =
                    params.allocationAndRetentionPriority.pre_emptionVulnerability;
            }
            ASN_STRUCT_FREE(asn_DEF_QosFlowSetupRequestList, list);
        }
    }

    if (const auto* ie = ::ngap::find_ie(transfer->protocolIEs, 130); ie != nullptr) {
        auto* ambr = static_cast<PDUSessionAggregateMaximumBitRate_t*>(
            ::ngap::decode_ie_value(&asn_DEF_PDUSessionAggregateMaximumBitRate, *ie));
        if (ambr != nullptr) {
            unsigned long dl = 0;
            unsigned long ul = 0;
            if (asn_INTEGER2ulong(&ambr->pDUSessionAggregateMaximumBitRateDL, &dl) == 0 &&
                asn_INTEGER2ulong(&ambr->pDUSessionAggregateMaximumBitRateUL, &ul) == 0) {
                out.ambr_dl_bps = dl;
                out.ambr_ul_bps = ul;
            }
            ASN_STRUCT_FREE(asn_DEF_PDUSessionAggregateMaximumBitRate, ambr);
        }
    }

    ASN_STRUCT_FREE(asn_DEF_PDUSessionResourceSetupRequestTransfer, transfer);
    if (!ok) {
        return std::nullopt;
    }
    return out;
}

sbi_core::http2::TlsConfig test_tls() {
    return sbi_core::http2::TlsConfig{
        .cert_path = CERTS_DIR "/hello-nf/cert.pem",
        .key_path = CERTS_DIR "/hello-nf/key.pem",
        .ca_path = CERTS_DIR "/ca/ca.crt",
        .verify = {},
    };
}

bool wait_reachable(sbi_core::http2::Client& client, const std::string& url, int max_attempts) {
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        sbi_core::http2::ClientRequest req;
        req.method = "GET";
        req.url = url;
        if (client.send(req).has_value()) {
            return true;
        }
        std::this_thread::sleep_for(100ms);
    }
    return false;
}

std::string fetch_smf_token() {
    sbi_core::http2::ClientRequest req;
    req.method = "POST";
    req.url = "https://127.0.0.1:7777/oauth2/token";
    req.headers.emplace("content-type", "application/x-www-form-urlencoded");
    req.body =
        "grant_type=client_credentials&nfInstanceId=" + std::string(nf_test::kTestAfInstanceId) +
        "&scope=nsmf-pdusession&targetNfType=SMF";
    auto resp = nf_test::send_token_request(req);
    if (!resp.has_value() || resp->status != 200) {
        return "";
    }
    return json::parse(resp->body).at("access_token").get<std::string>();
}

class IoContextThread {
public:
    explicit IoContextThread(boost::asio::io_context& ioc) : ioc_(ioc) {
        thread_ = std::thread([&ioc] { ioc.run(); });
    }
    ~IoContextThread() {
        ioc_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    IoContextThread(const IoContextThread&) = delete;
    IoContextThread& operator=(const IoContextThread&) = delete;

private:
    boost::asio::io_context& ioc_;
    std::thread thread_;
};

// One N1N2MessageTransfer as the AMF stand-in received it.
struct ReceivedTransfer {
    json data;
    std::vector<sbi_core::multipart::Part> parts;
};

std::optional<std::vector<std::uint8_t>>
part_bytes(const std::vector<sbi_core::multipart::Part>& parts, const std::string& content_id) {
    for (const auto& part : parts) {
        if (part.content_id.has_value() && *part.content_id == content_id) {
            return std::vector<std::uint8_t>(part.body.begin(), part.body.end());
        }
    }
    return std::nullopt;
}

} // namespace

TEST(SmfN2SetupTransfer, EncodesTheMandatoryIesAndTheGivenQosFlow) {
    smf::n2::SetupRequestTransferInput in;
    in.ul_teid = 0x0A0B0C0D;
    in.ul_ipv4 = {127, 0, 0, 8};
    in.flow.qfi = 5;
    in.flow.five_qi = 5;
    in.flow.arp_priority_level = 2;
    in.flow.arp_may_trigger_pre_emption = true;
    in.flow.arp_pre_emptable = false;
    in.ambr_dl_bps = 200'000'000;
    in.ambr_ul_bps = 100'000'000;

    const auto bytes = smf::n2::encode_setup_request_transfer(in);
    ASSERT_FALSE(bytes.empty());
    const auto decoded = decode_transfer(bytes);
    ASSERT_TRUE(decoded.has_value()) << "the transfer does not decode, or lacks its UL F-TEID";
    EXPECT_EQ(decoded->ul_teid, 0x0A0B0C0Du);
    EXPECT_EQ(decoded->ul_ipv4, (std::array<std::uint8_t, 4>{127, 0, 0, 8}));
    EXPECT_EQ(decoded->pdu_session_type, PDUSessionType_ipv4);
    EXPECT_EQ(decoded->qos_flow_count, 1u);
    EXPECT_EQ(decoded->qfi, 5);
    EXPECT_EQ(decoded->five_qi, 5);
    EXPECT_EQ(decoded->arp_priority_level, 2);
    EXPECT_EQ(decoded->pre_emption_capability, Pre_emptionCapability_may_trigger_pre_emption);
    EXPECT_EQ(decoded->pre_emption_vulnerability, Pre_emptionVulnerability_not_pre_emptable);
    ASSERT_TRUE(decoded->ambr_dl_bps.has_value());
    EXPECT_EQ(*decoded->ambr_dl_bps, 200'000'000ul);
    EXPECT_EQ(*decoded->ambr_ul_bps, 100'000'000ul);

    // Without a Session-AMBR the optional IE is left out; the mandatory ones stay.
    in.ambr_dl_bps.reset();
    in.ambr_ul_bps.reset();
    const auto without_ambr = decode_transfer(smf::n2::encode_setup_request_transfer(in));
    ASSERT_TRUE(without_ambr.has_value());
    EXPECT_FALSE(without_ambr->ambr_dl_bps.has_value());
    EXPECT_EQ(without_ambr->qfi, 5);
}

TEST(SmfN2SetupTransfer, EstablishmentN1N2CarriesN2SmInfoMatchingTheHandoverTransfer) {
    std::mutex mutex;
    std::vector<ReceivedTransfer> received;

    boost::asio::io_context amf_ioc;
    sbi_core::http2::Server amf(amf_ioc, "127.0.0.1", 7778, test_tls());
    amf.add_route("POST",
                  "/namf-comm/v1/ue-contexts/{ueContextId}/n1-n2-messages",
                  [&](const sbi_core::http2::Request& req) {
                      ReceivedTransfer r;
                      if (const auto ct = req.headers.find("content-type");
                          ct != req.headers.end()) {
                          if (auto parts = sbi_core::multipart::parse(ct->second, req.body);
                              parts.has_value()) {
                              r.parts = *parts;
                              for (const auto& part : r.parts) {
                                  if (part.content_type == "application/json") {
                                      r.data = json::parse(part.body, nullptr, false);
                                  }
                              }
                          }
                      }
                      {
                          std::lock_guard lock(mutex);
                          received.push_back(std::move(r));
                      }
                      sbi_core::http2::Response resp;
                      resp.status = 200;
                      resp.headers.emplace("content-type", "application/json");
                      resp.body = json{{"cause", "N1_N2_TRANSFER_INITIATED"}}.dump();
                      return resp;
                  });
    amf.start();
    IoContextThread amf_thread(amf_ioc);

    nf_test::SpawnedProcess nrf(NRF_PATH);
    ASSERT_GT(nrf.pid(), 0) << "failed to fork nrf";
    std::this_thread::sleep_for(200ms);
    // ADR-0554: the NRF issues no token towards an NF type with no registered instance.
    nf_test::register_test_nf("00000000-0000-4000-8000-0000000064f1", "AMF");
    nf_test::SpawnedProcess upf(UPF_PATH);
    ASSERT_GT(upf.pid(), 0) << "failed to fork upf";
    nf_test::SpawnedProcess smf(SMF_PATH);
    ASSERT_GT(smf.pid(), 0) << "failed to fork smf";
    nf_test::SpawnedProcess pcf(PCF_PATH);
    ASSERT_GT(pcf.pid(), 0) << "failed to fork pcf";

    sbi_core::http2::Client client(test_tls());
    ASSERT_TRUE(wait_reachable(
        client, "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/nonexistent/retrieve", 50))
        << "smf never became reachable";
    {
        sbi_core::http2::ClientRequest probe;
        probe.method = "GET";
        probe.url = "https://127.0.0.1:7783/npcf-am-policy-control/v1/policies/nonexistent";
        ASSERT_TRUE(nf_test::wait_until_ready([&] { return client.send(probe).has_value(); },
                                              std::chrono::seconds(5)))
            << "pcf never became reachable";
    }
    const std::string token = fetch_smf_token();
    ASSERT_FALSE(token.empty());

    // The SMF finds the UPF through the NRF on its own retry cadence; until the Sx association is
    // up a CreateSMContext gets no N4 session, so its N1N2MessageTransfer has no N2 SM
    // information. Retry with a fresh PDU session id until one does.
    std::optional<ReceivedTransfer> with_n2;
    std::string sm_context_ref;
    for (int attempt = 0; attempt < 40 && !with_n2.has_value(); ++attempt) {
        const auto pdu_session_id = 10 + attempt;
        sbi_core::multipart::Part json_part;
        json_part.content_type = "application/json";
        json_part.body =
            json{
                {"servingNfId", "00000000-0000-4000-8000-0000000000aa"},
                {"servingNetwork", json{{"mcc", "999"}, {"mnc", "70"}}},
                {"anType", "3GPP_ACCESS"},
                {"smContextStatusUri", "https://example.com/sm-status"},
                {"supi", "imsi-999700000000642"},
                {"pduSessionId", pdu_session_id},
                {"dnn", "internet"},
                {"sNssai", json{{"sst", 1}}},
                {"n1SmMsg", json{{"contentId", "n1SmMsg"}}},
            }
                .dump();
        sbi_core::multipart::Part n1_part;
        n1_part.content_type = "application/vnd.3gpp.5gnas";
        n1_part.content_id = "n1SmMsg";
        // TS 24.501 8.3.1: EPD 0x2E, PDU session id, PTI, message type 0xC1.
        const std::vector<std::uint8_t> establishment_request{
            0x2E, static_cast<std::uint8_t>(pdu_session_id), 0x01, 0xC1};
        n1_part.body.assign(establishment_request.begin(), establishment_request.end());
        const auto encoded = sbi_core::multipart::encode({json_part, n1_part});

        sbi_core::http2::ClientRequest create_req;
        create_req.method = "POST";
        create_req.url = "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts";
        create_req.headers.emplace("content-type", encoded.content_type_header);
        create_req.headers.emplace("authorization", "Bearer " + token);
        create_req.body = encoded.body;
        auto create_resp = client.send(create_req);
        if (create_resp.has_value() && create_resp->status == 201) {
            std::lock_guard lock(mutex);
            if (!received.empty() && received.back().data.contains("n2InfoContainer")) {
                with_n2 = received.back();
                const auto loc = create_resp->headers.find("location");
                if (loc != create_resp->headers.end()) {
                    sm_context_ref = loc->second.substr(loc->second.rfind('/') + 1);
                }
            }
        }
        if (!with_n2.has_value()) {
            std::this_thread::sleep_for(250ms);
        }
    }
    ASSERT_TRUE(with_n2.has_value())
        << "no N1N2MessageTransfer carried N2 SM information after the UPF association";
    ASSERT_FALSE(sm_context_ref.empty());

    // TS29518 N2InfoContainer / N2SmInformation / N2InfoContent.
    const auto& container = with_n2->data.at("n2InfoContainer");
    EXPECT_EQ(container.at("n2InformationClass"), "SM");
    const auto& sm_info = container.at("smInfo");
    EXPECT_EQ(sm_info.at("pduSessionId"), with_n2->data.at("pduSessionId"));
    EXPECT_EQ(sm_info.at("sNssai").at("sst"), 1);
    EXPECT_EQ(sm_info.at("n2InfoContent").at("ngapIeType"), "PDU_RES_SETUP_REQ");
    const auto content_id =
        sm_info.at("n2InfoContent").at("ngapData").at("contentId").get<std::string>();
    // The N1 SM container still goes with it.
    EXPECT_TRUE(part_bytes(with_n2->parts, "n1Message").has_value());

    const auto n2_bytes = part_bytes(with_n2->parts, content_id);
    ASSERT_TRUE(n2_bytes.has_value()) << "no binary part named " << content_id;
    for (const auto& part : with_n2->parts) {
        if (part.content_id.has_value() && *part.content_id == content_id) {
            EXPECT_EQ(part.content_type, "application/vnd.3gpp.ngap");
        }
    }
    const auto establishment = decode_transfer(*n2_bytes);
    ASSERT_TRUE(establishment.has_value()) << "the N2 SM information does not decode";
    EXPECT_NE(establishment->ul_teid, 0u) << "the UL F-TEID is not the UPF's allocated one";
    EXPECT_EQ(establishment->pdu_session_type, PDUSessionType_ipv4);
    ASSERT_EQ(establishment->qos_flow_count, 1u);
    // Disclosed simplification (ADR-0642, unchanged): QFI = 5QI & 0x3F.
    EXPECT_EQ(establishment->qfi, establishment->five_qi & 0x3F);

    // N2 handover preparation describes the same QoS flow and the same UPF F-TEID.
    sbi_core::http2::ClientRequest ho;
    ho.method = "POST";
    ho.url = "https://127.0.0.1:7779/nsmf-pdusession/v1/sm-contexts/" + sm_context_ref + "/modify";
    ho.headers.emplace("content-type", "application/json");
    ho.headers.emplace("authorization", "Bearer " + token);
    ho.body = json{{"n2SmInfoType", "HANDOVER_REQUIRED"}}.dump();
    auto ho_resp = client.send(ho);
    ASSERT_TRUE(ho_resp.has_value());
    ASSERT_EQ(ho_resp->status, 200) << ho_resp->body;
    const auto ct = ho_resp->headers.find("content-type");
    ASSERT_NE(ct, ho_resp->headers.end());
    auto ho_parts = sbi_core::multipart::parse(ct->second, ho_resp->body);
    ASSERT_TRUE(ho_parts.has_value());
    const auto ho_bytes = part_bytes(*ho_parts, "n2SmInfo");
    ASSERT_TRUE(ho_bytes.has_value());
    const auto handover = decode_transfer(*ho_bytes);
    ASSERT_TRUE(handover.has_value());
    EXPECT_EQ(handover->ul_teid, establishment->ul_teid);
    EXPECT_EQ(handover->ul_ipv4, establishment->ul_ipv4);
    EXPECT_EQ(handover->qfi, establishment->qfi);
    EXPECT_EQ(handover->five_qi, establishment->five_qi);
    EXPECT_EQ(handover->arp_priority_level, establishment->arp_priority_level);
    EXPECT_EQ(handover->ambr_dl_bps, establishment->ambr_dl_bps);
}
