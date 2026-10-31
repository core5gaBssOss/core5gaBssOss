#pragma once

// REVIEW-5ae0ef1 SUS-02, ADR-0538: the expiries the NEF's northbound and Nnef APIs make
// mandatory, as pure functions over the stored documents so they can be tested without a NEF.
// A sweeper thread in main.cpp runs them over the stores; the "confirm" helpers cap what a
// consumer proposes at create/update time.

#include <nlohmann/json.hpp>

#include <chrono>

namespace nef {

using Clock = std::chrono::system_clock;

// What a sweep does with one stored document.
enum class SweepAction {
    keep,    // unchanged
    rewrite, // part of it expired and was removed in place
    remove,  // the whole resource expired
};

// TS 29.122 4.4.2.3: "When the monitoring duration indicated by the property
// "monitorExpireTime" is reached, the SCEF shall delete the related event subscription and event
// configuration locally." A MonitoringEventSubscription.
SweepAction expire_monitoring(const nlohmann::json& doc, Clock::time_point now);

// TS 29.591 4.2.2.2.2: eventsRepInfo.monDur is the NEF-selected expiry of a NefEventExposureSubsc
// (NOTE 3: a PUT can extend it). Per-event eventRepInfo.monDur (PerEventRepReq) is not a
// subscription lifetime and is not swept.
SweepAction expire_event_exposure(const nlohmann::json& doc, Clock::time_point now);

// TS 29.522 5.15.4.3.2 TimeSyncExposureSubsc.expiry: "after which ... the subscription becomes
// invalid". TimeSyncExposureConfig has no expiry, so a configuration stored beside the
// subscriptions is kept.
SweepAction expire_time_sync(const nlohmann::json& doc, Clock::time_point now);

// TS 29.122 5.13.2.1.2 NpConfiguration.validityTime: "Identifies when the network parameter
// expires and shall be deleted locally if it expires."
SweepAction expire_np_configuration(const nlohmann::json& doc, Clock::time_point now);

// TS 29.522 5.7.2.3.10 NpConfigParams.validityTime, inside 5GLanParametersProvision.5gLanParams:
// the same text; only the npConfigParams member is deleted, the 5G LAN provisioning stays.
SweepAction expire_5glan_np_config(nlohmann::json& doc, Clock::time_point now);

// TS 29.122 5.10.2.2.2 CpParameterSet.validityTime: "Identifies when the CP parameter set expires
// and shall be deleted." A CpInfo drops its expired sets; one left with none is removed (CpInfo
// requires cpParameterSets, minItems 1). A CpParameterSet stored on its own is removed.
SweepAction expire_cp_parameter_sets(nlohmann::json& doc, Clock::time_point now);

// The value at `pointer` (an RFC 3339 DateTime the consumer proposed), when present and
// parseable, replaced by the expiry the NEF confirms: no later than now + max_lifetime
// (sbi_core::confirm_expiry). Absent: nothing is added -- these APIs give the consumer, not the
// NEF, the choice of an expiry. Unparseable: left alone; the sweep then ignores it too.
void confirm_proposed_expiry(nlohmann::json& doc,
                             const nlohmann::json::json_pointer& pointer,
                             Clock::time_point now,
                             std::chrono::seconds max_lifetime);

} // namespace nef
