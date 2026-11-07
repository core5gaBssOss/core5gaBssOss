// REVIEW-5ae0ef1 SUS-02 follow-up (ADR-0565): the one place the UDR refuses a write under
// /subscription-data/{ueId}/... for a user it has not provisioned, with 404 USER_NOT_FOUND
// (TS 29.504 V19.8.0 table 6.1.6-2). A registrar rather than a check inside each handler, so a
// write route added later is guarded by registering it, not by remembering to.
#pragma once

#include "sbi_core/http2_server.hpp"
#include "sbi_core/json_body.hpp"

#include <regex>
#include <string>
#include <utility>

#include "stores.hpp"

namespace udr {

// True for the two non-user forms TS29505_Subscription_Data.yaml allows as the ueId of
// PUT/DELETE .../pp-data-store/{afInstanceId} -- the only writes under {ueId} whose schema has them
// (`anyOf: [VarUeId, ExtGroupId, {pattern: '^anyUE$'}]`). The pattern is the YAML's ExtGroupId
// (TS29503_Nudm_SDM.yaml).
inline bool is_non_user_pp_data_store_id(const std::string& ue_id) {
    static const std::regex ext_group_id("^extgroupid-[^@]+@[^@]+$");
    return ue_id == "anyUE" || std::regex_match(ue_id, ext_group_id);
}

// Forwards to Server::add_route. A non-GET route under `{api_root}/subscription-data/{ueId}/` gets
// its handler wrapped: authorization has already run by then (http2_server.cpp), and the check
// precedes body validation, so an unknown user gets the 404 and not a 400 for its body. A database
// failure throws out of exists() and the server answers 5xx: a write is never let through on one.
// `store` must outlive `server`.
class ProvisionedWriteRoutes {
public:
    ProvisionedWriteRoutes(sbi_core::http2::Server& server,
                           ProvisionedDataStore& store,
                           const std::string& api_root)
        : server_(server), store_(store), guarded_prefix_(api_root + "/subscription-data/{ueId}/"),
          pp_data_store_pattern_(guarded_prefix_ + "pp-data-store/{afInstanceId}") {}

    void add_route(std::string method,
                   std::string pattern,
                   sbi_core::http2::Handler handler,
                   sbi_core::http2::RouteAuth auth = sbi_core::http2::RouteAuth::OAuth2) {
        if (method != "GET" && pattern.starts_with(guarded_prefix_)) {
            handler = [&store = store_,
                       allow_non_user = pattern == pp_data_store_pattern_,
                       inner = std::move(handler)](
                          const sbi_core::http2::Request& req) -> sbi_core::http2::Response {
                const std::string& ue_id = req.path_params.at("ueId");
                if ((allow_non_user && is_non_user_pp_data_store_id(ue_id)) ||
                    store.exists(ue_id)) {
                    return inner(req);
                }
                return sbi_core::http2::problem_response(
                    404, "Not Found", "The UDR has no provisioned user " + ue_id, "USER_NOT_FOUND");
            };
        }
        server_.add_route(std::move(method), std::move(pattern), std::move(handler), auth);
    }

private:
    sbi_core::http2::Server& server_;
    ProvisionedDataStore& store_;
    std::string guarded_prefix_;
    std::string pp_data_store_pattern_;
};

} // namespace udr
