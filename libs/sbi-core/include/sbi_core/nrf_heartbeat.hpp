#pragma once

#include "sbi_core/http2_client.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <functional>
#include <string>

// The NF side of the NRF heart-beat (TS 29.510 5.2.2.3.2), shared by every NF's NRF lifecycle
// (ADR-0506). Before this each NF's heartbeat loop only logged a failed PATCH, so an NRF that lost
// its registry (it keeps it in memory, ADR-0015) left every NF unregistered until the NF itself was
// restarted (found bringing the lab up, ADR-0505).
//
// One heartbeat is the PATCH of step 1 ("replace" /nfStatus with REGISTERED). The NRF answers 404
// when the instance "is not found in the list of registered NF Instances" (step 2b); the NF then
// registers again with the profile it registered with (NFRegister, 5.2.2.2.2). TS 29.510 states
// the 404 but not the NF's reaction to it; re-registering is this project's choice (ADR-0506; the
// restoration procedures of TS 23.527 clause 6.2 are not available offline).
//
// A 401 is treated the same way. With ADR-0499 an NRF that does not know the instance refuses it
// a token (invalid_client), so once the NF's cached token has expired its heartbeat goes without
// one and is answered 401, never 404. NFRegister of an unknown instance needs no token; replacing
// a known one does, so for an NF the NRF still knows the PUT is refused the same way and nothing
// changes.

namespace sbi_core::nrf {

struct Registration {
    std::string nrf_base;       // e.g. "https://nrf:7777"
    std::string nf_instance_id; // the {nfInstanceID} the profile was registered under
    nlohmann::json profile;     // the NFProfile NFRegister sends
    std::string log_name;       // the NF's log prefix, e.g. "amf"
    // After every successful NFRegister: a token refused while the NF was not registered is asked
    // for again (ADR-0499). An NF with its own TokenSource (SEPP, SCP) passes its clear_backoff.
    std::function<void()> on_registered;
};

enum class HeartbeatResult {
    Accepted,     // the NRF took the heartbeat
    Reregistered, // the NRF did not know the instance; NFRegister succeeded
    Failed,       // no answer, another error, or the NFRegister after a 404/401 failed
};

// NFRegister (TS 29.510 5.2.2.2.2): PUT the profile; true on 200 or 201, then on_registered().
bool register_instance(http2::Client& client, const Registration& reg);

// One heart-beat, re-registering on 404 or 401 (above). Logs every outcome but Accepted.
HeartbeatResult heartbeat(http2::Client& client, const Registration& reg);

// heartbeat() every `interval`, for the life of the process.
[[noreturn]] void
run_heartbeats(http2::Client& client, const Registration& reg, std::chrono::seconds interval);

} // namespace sbi_core::nrf
