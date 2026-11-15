// ADR-0574: NRF NFUpdate partial update / heart-beat (TS 29.510 5.2.2.3.1B, 5.2.2.3.2) and
// UpdateSubscription (5.2.2.5.6) request checks, and the registry's atomic update. Pure logic, no
// ports: safe to run while the CI runner is busy.

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

#include "nf_update.hpp"
#include "registry.hpp"

#include <gtest/gtest.h>

namespace {

using nlohmann::json;

constexpr const char* kId = "4947a69a-f61b-4bc1-b9da-47c9c5d14b64";

json profile() {
    return json{{"nfInstanceId", kId}, {"nfType", "AMF"}, {"nfStatus", "REGISTERED"}};
}

std::optional<std::string> accept_all(const json&) {
    return std::nullopt;
}

std::string cause_of(const sbi_core::http2::Response& r) {
    return json::parse(r.body).value("cause", "");
}

json ops(std::initializer_list<json> items) {
    return json(items);
}

} // namespace

TEST(NrfNfUpdate, HeartBeatReplaceOfNfStatusAndLoadIsApplied) {
    // The 5.2.2.3.2 EXAMPLE body.
    const auto out = nrf::patch_nf_profile(
        profile(),
        ops({{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "REGISTERED"}},
             {{"op", "add"}, {"path", "/load"}, {"value", 50}}}),
        kId,
        std::nullopt,
        accept_all);
    ASSERT_TRUE(out.has_value()) << out.error().body;
    EXPECT_EQ(out->at("load"), 50);
}

TEST(NrfNfUpdate, ReplacingANonExistingIeIs409) {
    // 5.2.2.3.1B step 2b: "to change value of a non-existing IE" -> 409.
    const auto out =
        nrf::patch_nf_profile(profile(),
                              ops({{{"op", "replace"}, {"path", "/priority"}, {"value", 1}}}),
                              kId,
                              std::nullopt,
                              accept_all);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().status, 409);
}

TEST(NrfNfUpdate, AFailedTestOpIs409) {
    const auto out = nrf::patch_nf_profile(
        profile(),
        ops({{{"op", "test"}, {"path", "/nfStatus"}, {"value", "SUSPENDED"}}}),
        kId,
        std::nullopt,
        accept_all);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().status, 409);
}

TEST(NrfNfUpdate, APatchItemWithoutOpIs400InvalidMsgFormat) {
    const auto out = nrf::patch_nf_profile(profile(),
                                           ops({{{"path", "/nfStatus"}, {"value", "REGISTERED"}}}),
                                           kId,
                                           std::nullopt,
                                           accept_all);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().status, 400);
    EXPECT_EQ(cause_of(out.error()), "INVALID_MSG_FORMAT");
}

TEST(NrfNfUpdate, RemovingAMandatoryIeIsModificationNotAllowed) {
    const auto out = nrf::patch_nf_profile(
        profile(), ops({{{"op", "remove"}, {"path", "/nfType"}}}), kId, std::nullopt, accept_all);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().status, 403);
    EXPECT_EQ(cause_of(out.error()), "MODIFICATION_NOT_ALLOWED");
}

TEST(NrfNfUpdate, ChangingNfInstanceIdIsModificationNotAllowed) {
    const auto out =
        nrf::patch_nf_profile(profile(),
                              ops({{{"op", "replace"},
                                    {"path", "/nfInstanceId"},
                                    {"value", "5ba9a927-1d31-4c8e-8a10-000000000002"}}}),
                              kId,
                              std::nullopt,
                              accept_all);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().status, 403);
    EXPECT_EQ(cause_of(out.error()), "MODIFICATION_NOT_ALLOWED");
}

TEST(NrfNfUpdate, ReplacingTheWholeProfileIsModificationNotAllowed) {
    const auto out = nrf::patch_nf_profile(profile(),
                                           ops({{{"op", "replace"}, {"path", ""}, {"value", 1}}}),
                                           kId,
                                           std::nullopt,
                                           accept_all);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().status, 403);
}

TEST(NrfNfUpdate, AProfileTheValidatorRejectsIs400) {
    const auto out =
        nrf::patch_nf_profile(profile(),
                              ops({{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "BOGUS"}}}),
                              kId,
                              std::nullopt,
                              [](const json& p) -> std::optional<std::string> {
                                  if (p.at("nfStatus") == "BOGUS") {
                                      return "bad nfStatus";
                                  }
                                  return std::nullopt;
                              });
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().status, 400);
    EXPECT_EQ(cause_of(out.error()), "UNSPECIFIED_MSG_FAILURE");
}

TEST(NrfNfUpdate, IfMatchWithTheCurrentTagIsApplied) {
    const auto tag = nrf::profile_etag(profile());
    const auto out = nrf::patch_nf_profile(
        profile(),
        ops({{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "UNDISCOVERABLE"}}}),
        kId,
        tag,
        accept_all);
    ASSERT_TRUE(out.has_value()) << out.error().body;
    EXPECT_EQ(out->at("nfStatus"), "UNDISCOVERABLE");
}

TEST(NrfNfUpdate, IfMatchWithAnotherTagIs412) {
    // 5.2.2.3.1B step 2b: a different entity tag -> 412 Precondition Failed.
    const auto out = nrf::patch_nf_profile(
        profile(),
        ops({{{"op", "replace"}, {"path", "/nfStatus"}, {"value", "UNDISCOVERABLE"}}}),
        kId,
        std::string("\"stale\""),
        accept_all);
    ASSERT_FALSE(out.has_value());
    EXPECT_EQ(out.error().status, 412);
}

TEST(NrfNfUpdate, IfMatchListsStarAndWeakTags) {
    const std::string tag = nrf::profile_etag(profile());
    EXPECT_TRUE(nrf::if_match_satisfied("*", tag));
    EXPECT_TRUE(nrf::if_match_satisfied("\"a\", " + tag, tag));
    EXPECT_FALSE(nrf::if_match_satisfied("W/" + tag, tag)); // strong comparison only
    EXPECT_FALSE(nrf::if_match_satisfied("\"a\", \"b\"", tag));
}

TEST(NrfNfUpdate, TheEntityTagFollowsTheContent) {
    auto changed = profile();
    changed["load"] = 1;
    EXPECT_EQ(nrf::profile_etag(profile()), nrf::profile_etag(profile()));
    EXPECT_NE(nrf::profile_etag(profile()), nrf::profile_etag(changed));
}

TEST(NrfNfUpdate, RegistryUpdateKeepsTheProfileWhenTheEditFails) {
    nrf::NfRegistry registry;
    registry.put(kId, profile());
    auto outcome = registry.update(kId, [](const json& current) {
        return nrf::patch_nf_profile(current,
                                     json::array({{{"op", "remove"}, {"path", "/nfType"}}}),
                                     kId,
                                     std::nullopt,
                                     accept_all);
    });
    ASSERT_TRUE(outcome.has_value());
    EXPECT_FALSE(outcome->has_value());
    EXPECT_EQ(registry.get(kId)->at("nfType"), "AMF");
}

TEST(NrfNfUpdate, RegistryUpdateOfAnUnknownInstanceIsNullopt) {
    nrf::NfRegistry registry;
    auto outcome = registry.update(kId, [](const json& current) {
        return tl::expected<json, sbi_core::http2::Response>(current);
    });
    EXPECT_FALSE(outcome.has_value());
}

TEST(NrfUpdateSubscription, OnlyAReplaceOfValidityTimeIsAccepted) {
    const auto ok = nrf::subscription_validity_time(json::array(
        {{{"op", "replace"}, {"path", "/validityTime"}, {"value", "2030-12-30T23:20:50Z"}}}));
    ASSERT_TRUE(ok.has_value()) << ok.error().body;
    EXPECT_EQ(*ok, "2030-12-30T23:20:50Z");

    for (const json& bad :
         {json::array({{{"op", "replace"}, {"path", "/nfStatusNotificationUri"}, {"value", "x"}}}),
          json::array(
              {{{"op", "add"}, {"path", "/validityTime"}, {"value", "2030-12-30T23:20:50Z"}}}),
          json::array({{{"op", "replace"}, {"path", "/validityTime"}, {"value", "tomorrow"}}}),
          json::array({{{"op", "replace"}, {"path", "/validityTime"}}})}) {
        const auto out = nrf::subscription_validity_time(bad);
        ASSERT_FALSE(out.has_value()) << bad.dump();
        EXPECT_EQ(out.error().status, 400);
        EXPECT_EQ(cause_of(out.error()), "INVALID_MSG_FORMAT");
    }
}

TEST(NrfUpdateSubscription, SetValidityTimeOfAnUnknownSubscriptionIsFalse) {
    nrf::SubscriptionRegistry registry;
    EXPECT_FALSE(registry.set_validity_time("missing", "2030-12-30T23:20:50Z"));
    const auto created = registry.create(json{{"nfStatusNotificationUri", "https://x/n"}});
    EXPECT_TRUE(registry.set_validity_time(created.at("subscriptionId").get<std::string>(),
                                           "2030-12-30T23:20:50Z"));
}

TEST(NrfDeregister, RemovingAnUnknownInstanceReportsIt) {
    // 5.2.2.4.2 step 2b: the route answers 404 when remove() reports nothing was removed.
    nrf::NfRegistry registry;
    EXPECT_FALSE(registry.remove(kId));
    registry.put(kId, profile());
    EXPECT_TRUE(registry.remove(kId));
}
