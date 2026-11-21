# ADR-0612: shared TM Forum Open API error helper (BSS SBI-03 PR 1). Builds the TMF `Error` body
# (the generated tmf_gen::Error, libs/tmf-generated, from
# specs/tmforum/TMF640-ServiceActivation-v4.0.0.swagger.json) for the BSS TM Forum servers, which
# answer errors in the TMF shape rather than as 3GPP ProblemDetails. No BSS service links it yet:
# each adopts it in its own later PR.

find_package(nlohmann_json CONFIG REQUIRED)

add_library(tmf_core STATIC
    src/error.cpp
)

target_include_directories(tmf_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(tmf_core PUBLIC
    5gc_project_options
    sbi_core
    tmf_generated
    nlohmann_json::nlohmann_json
)

target_compile_features(tmf_core PUBLIC cxx_std_23)
