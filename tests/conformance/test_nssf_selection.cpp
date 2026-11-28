// ADR-0640: Nnssf_NSSelection_Get decision (nfs/nssf/src/selection.cpp) against TS 29.531
// clause 5.2.2.2 and Table 6.1.6.2.2-1, using TS29531_Nnssf_NSSelection.yaml /
// _NSSAIAvailability.yaml shapes.

#include <nlohmann/json.hpp>

#include "../../nfs/nssf/src/selection.hpp"

#include <gtest/gtest.h>

namespace {

sbi_gen::Snssai snssai(std::int64_t sst, std::optional<std::string> sd = std::nullopt) {
    sbi_gen::Snssai s;
    s.sst = sst;
    s.sd = std::move(sd);
    return s;
}

sbi_gen::SubscribedSnssai subscribed(std::int64_t sst, bool is_default) {
    sbi_gen::SubscribedSnssai s;
    s.subscribedSnssai = snssai(sst);
    s.defaultIndication = is_default;
    return s;
}

sbi_gen::Tai_CommonData tai(const std::string& tac) {
    sbi_gen::Tai_CommonData t;
    t.plmnId.mcc = "999";
    t.plmnId.mnc = "70";
    t.tac = tac;
    return t;
}

// One AMF's NssaiAvailabilityInfo: TA `tac` supports only the listed SSTs.
nlohmann::json availability(const std::string& tac, std::vector<int> ssts) {
    nlohmann::json list = nlohmann::json::array();
    for (int sst : ssts) {
        list.push_back({{"sst", sst}});
    }
    return {{"supportedNssaiAvailabilityData",
             nlohmann::json::array(
                 {{{"tai", {{"plmnId", {{"mcc", "999"}, {"mnc", "70"}}}, {"tac", tac}}},
                   {"supportedSnssaiList", list}}})}};
}

const std::vector<sbi_gen::Snssai> kCatalog = {snssai(1), snssai(2)};
const std::string kNrfDisc = "https://nrf.example:7777/nnrf-disc/v1";

} // namespace

// 5.2.2.2.3 step 2b: an S-NSSAI the NSSF does not support -> no result (403 SNSSAI_NOT_SUPPORTED),
// not a 200 with an empty selection (the finding behind ADR-0640).
TEST(NssfSelection, PduSessionForAnUnsupportedSnssaiFindsNoSlice) {
    nssf::SelectionRequest req{.procedure = nssf::SelectionProcedure::PduSession,
                               .requested = std::vector{snssai(3)}};
    EXPECT_FALSE(nssf::decide_slice_selection(kCatalog, req, {}, kNrfDisc).has_value());
}

// 5.2.2.2.3 step 2a: the 200 carries the NRF to use within the slice (nsiInformation.nrfId).
TEST(NssfSelection, PduSessionForASupportedSnssaiAnswersNsiInformation) {
    nssf::SelectionRequest req{.procedure = nssf::SelectionProcedure::PduSession,
                               .requested = std::vector{snssai(1)}};
    auto out = nssf::decide_slice_selection(kCatalog, req, {}, kNrfDisc);
    ASSERT_TRUE(out.has_value());
    ASSERT_TRUE(out->nsiInformation.has_value());
    EXPECT_EQ(out->nsiInformation->nrfId, kNrfDisc);
    EXPECT_FALSE(out->allowedNssaiList.has_value());
}

TEST(NssfSelection, RegistrationWithNothingSupportedFindsNoSlice) {
    nssf::SelectionRequest req{.procedure = nssf::SelectionProcedure::Registration,
                               .requested = std::vector{snssai(3)},
                               .subscribed = std::vector{subscribed(3, true)}};
    EXPECT_FALSE(nssf::decide_slice_selection(kCatalog, req, {}, kNrfDisc).has_value());
}

// The finding behind ADR-0640: an AMF reported TA 000001 supports only SST 1, so SST 2 is rejected
// in that TA even though the PLMN catalog has it.
TEST(NssfSelection, RegistrationRejectsInTaWhatTheAvailabilityStoreWithdrew) {
    nssf::SelectionRequest req{.procedure = nssf::SelectionProcedure::Registration,
                               .requested = std::vector{snssai(1), snssai(2)},
                               .subscribed = std::vector{subscribed(1, true), subscribed(2, false)},
                               .tai = tai("000001")};
    auto out = nssf::decide_slice_selection(kCatalog, req, {availability("000001", {1})}, kNrfDisc);
    ASSERT_TRUE(out.has_value());
    ASSERT_TRUE(out->allowedNssaiList.has_value());
    ASSERT_EQ(out->allowedNssaiList->at(0).allowedSnssaiList.size(), 1u);
    EXPECT_EQ(out->allowedNssaiList->at(0).allowedSnssaiList[0].allowedSnssai.sst, 1);
    ASSERT_TRUE(out->rejectedNssaiInTa.has_value());
    ASSERT_EQ(out->rejectedNssaiInTa->size(), 1u);
    EXPECT_EQ(out->rejectedNssaiInTa->at(0).sst, 2);
    EXPECT_FALSE(out->rejectedNssaiInPlmn.has_value());
}

TEST(NssfSelection, PduSessionInATaThatDoesNotSupportTheSliceFindsNoSlice) {
    nssf::SelectionRequest req{.procedure = nssf::SelectionProcedure::PduSession,
                               .requested = std::vector{snssai(2)},
                               .tai = tai("000001")};
    EXPECT_FALSE(
        nssf::decide_slice_selection(kCatalog, req, {availability("000001", {1})}, kNrfDisc)
            .has_value());
}

// No AMF reported this TA: the catalog decides alone (disclosed in ADR-0640).
TEST(NssfSelection, ATaWithNoAvailabilityRecordFallsBackToTheCatalog) {
    nssf::SelectionRequest req{.procedure = nssf::SelectionProcedure::PduSession,
                               .requested = std::vector{snssai(2)},
                               .tai = tai("000002")};
    EXPECT_TRUE(nssf::decide_slice_selection(kCatalog, req, {availability("000001", {1})}, kNrfDisc)
                    .has_value());
}

// Table 6.1.6.2.2-1 allowedNssaiList: none of the Requested NSSAI permitted -> the default
// subscribed S-NSSAI available in the TA; configuredNssai because the request had an S-NSSAI not
// valid in the serving PLMN.
TEST(NssfSelection, RegistrationFallsBackToTheDefaultSubscribedSnssai) {
    nssf::SelectionRequest req{.procedure = nssf::SelectionProcedure::Registration,
                               .requested = std::vector{snssai(3)},
                               .subscribed =
                                   std::vector{subscribed(1, true), subscribed(2, false)}};
    auto out = nssf::decide_slice_selection(kCatalog, req, {}, kNrfDisc);
    ASSERT_TRUE(out.has_value());
    ASSERT_TRUE(out->allowedNssaiList.has_value());
    ASSERT_EQ(out->allowedNssaiList->at(0).allowedSnssaiList.size(), 1u);
    EXPECT_EQ(out->allowedNssaiList->at(0).allowedSnssaiList[0].allowedSnssai.sst, 1);
    ASSERT_TRUE(out->rejectedNssaiInPlmn.has_value());
    ASSERT_TRUE(out->configuredNssai.has_value());
    EXPECT_EQ(out->configuredNssai->size(), 2u);
}

TEST(NssfSelection, RegistrationWithAllRequestedValidHasNoConfiguredNssai) {
    nssf::SelectionRequest req{.procedure = nssf::SelectionProcedure::Registration,
                               .requested = std::vector{snssai(1)},
                               .subscribed = std::vector{subscribed(1, true)}};
    auto out = nssf::decide_slice_selection(kCatalog, req, {}, kNrfDisc);
    ASSERT_TRUE(out.has_value());
    EXPECT_FALSE(out->configuredNssai.has_value());
}

TEST(NssfSelection, UeConfigurationUpdateAnswersConfiguredNssai) {
    nssf::SelectionRequest req{.procedure = nssf::SelectionProcedure::UeConfigurationUpdate,
                               .subscribed =
                                   std::vector{subscribed(1, true), subscribed(3, false)}};
    auto out = nssf::decide_slice_selection(kCatalog, req, {}, kNrfDisc);
    ASSERT_TRUE(out.has_value());
    ASSERT_TRUE(out->configuredNssai.has_value());
    ASSERT_EQ(out->configuredNssai->size(), 1u);
    EXPECT_EQ(out->configuredNssai->at(0).configuredSnssai.sst, 1);
}

// The AuthorizedNetworkSliceInfo a 200 carries round-trips through the generated DTO.
TEST(NssfSelection, ResultSerializesToTheYamlShape) {
    nssf::SelectionRequest req{.procedure = nssf::SelectionProcedure::Registration,
                               .requested = std::vector{snssai(1), snssai(2)},
                               .tai = tai("000001")};
    auto out = nssf::decide_slice_selection(kCatalog, req, {availability("000001", {1})}, kNrfDisc);
    ASSERT_TRUE(out.has_value());
    const nlohmann::json j = *out;
    EXPECT_TRUE(j.contains("allowedNssaiList"));
    EXPECT_TRUE(j.contains("rejectedNssaiInTa"));
    EXPECT_NO_THROW((void)j.get<sbi_gen::AuthorizedNetworkSliceInfo>());
}

TEST(NssfSelection, ExtSnssaiMatchesSdWildcardAndRanges) {
    EXPECT_TRUE(nssf::ext_snssai_matches({{"sst", 1}, {"sd", "0000AA"}}, snssai(1, "0000aa")));
    EXPECT_FALSE(nssf::ext_snssai_matches({{"sst", 1}, {"sd", "0000AA"}}, snssai(1, "0000ab")));
    EXPECT_FALSE(nssf::ext_snssai_matches({{"sst", 1}}, snssai(1, "000001")));
    EXPECT_TRUE(nssf::ext_snssai_matches({{"sst", 1}, {"wildcardSd", true}}, snssai(1, "123456")));
    const nlohmann::json ranged = {
        {"sst", 1},
        {"sdRanges", nlohmann::json::array({{{"start", "000010"}, {"end", "0000FF"}}})}};
    EXPECT_TRUE(nssf::ext_snssai_matches(ranged, snssai(1, "000020")));
    EXPECT_FALSE(nssf::ext_snssai_matches(ranged, snssai(1, "000100")));
    EXPECT_FALSE(nssf::ext_snssai_matches(ranged, snssai(1)));
    EXPECT_FALSE(nssf::ext_snssai_matches({{"sst", 2}}, snssai(1)));
}

TEST(NssfSelection, TaiListCoversATaToo) {
    nlohmann::json info = {
        {"supportedNssaiAvailabilityData",
         nlohmann::json::array(
             {{{"tai", {{"plmnId", {{"mcc", "999"}, {"mnc", "70"}}}, {"tac", "000009"}}},
               {"taiList",
                nlohmann::json::array(
                    {{{"plmnId", {{"mcc", "999"}, {"mnc", "70"}}}, {"tac", "000003"}}})},
               {"supportedSnssaiList", nlohmann::json::array({{{"sst", 2}}})}}})}};
    auto set = nssf::ta_supported_snssais({info}, tai("000003"));
    ASSERT_TRUE(set.has_value());
    ASSERT_EQ(set->size(), 1u);
    EXPECT_FALSE(nssf::ta_supported_snssais({info}, tai("000004")).has_value());
}
