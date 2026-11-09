# ADR-0568: generates C++ DTOs + JSON serializers from the TM Forum Open API swagger in
# specs/tmforum/ (ADR-0558: TMF640 first) with the same tools/sbi-codegen generator the 3GPP DTOs
# use, into their own namespace `tmf_gen`, and compiles them into the tmf_generated library. Same
# staging + sync pattern as libs/sbi-generated/CMakeLists.txt (ADR-0360); see that file for why.

set(TMF_CODEGEN_SCRIPT ${CMAKE_SOURCE_DIR}/tools/sbi-codegen/generate.py)
set(TMF_CODEGEN_SYNC ${CMAKE_SOURCE_DIR}/tools/sbi-codegen/sync_generated.py)
set(TMF_CODEGEN_SPECS_DIR ${CMAKE_SOURCE_DIR}/specs/tmforum)
set(TMF_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated/tmf_gen)
set(TMF_GENERATED_STAGING_DIR ${TMF_GENERATED_DIR}.staging)

# One generator run per TM Forum API: each file carries its own provenance line and commit
# (specs/tmforum/SOURCES.md).
set(TMF640_FILE TMF640-ServiceActivation-v4.0.0.swagger.json)
set(TMF640_ARGS
    ${TMF640_FILE}
    --specs-dir ${TMF_CODEGEN_SPECS_DIR}
    --commit 44d0e9e81f3d27239fa31e4c86b568dd22eeacf1
    --namespace tmf_gen
    --provenance "TM Forum Open API TMF640 Service Activation v4.0.0, tmforum-apis/TMF640_ActivationConfiguration"
    --specs-path specs/tmforum
)

file(GLOB_RECURSE TMF_CODEGEN_TOOL_SOURCES
    ${CMAKE_SOURCE_DIR}/tools/sbi-codegen/*.py
    ${CMAKE_SOURCE_DIR}/tools/sbi-codegen/templates/*.j2
)

file(REMOVE_RECURSE ${TMF_GENERATED_STAGING_DIR})
execute_process(
    COMMAND python3 ${TMF_CODEGEN_SCRIPT} ${TMF640_ARGS} --out ${TMF_GENERATED_STAGING_DIR}
    RESULT_VARIABLE TMF_CODEGEN_RESULT
)
if(NOT TMF_CODEGEN_RESULT EQUAL 0)
    message(FATAL_ERROR "tools/sbi-codegen/generate.py failed for the TM Forum specs (see above)")
endif()
execute_process(
    COMMAND python3 ${TMF_CODEGEN_SYNC} ${TMF_GENERATED_STAGING_DIR} ${TMF_GENERATED_DIR}
    RESULT_VARIABLE TMF_CODEGEN_SYNC_RESULT
)
if(NOT TMF_CODEGEN_SYNC_RESULT EQUAL 0)
    message(FATAL_ERROR "tools/sbi-codegen/sync_generated.py failed for the TM Forum specs")
endif()

file(GLOB TMF_GENERATED_SOURCES ${TMF_GENERATED_DIR}/*.cpp)

add_custom_command(
    OUTPUT ${TMF_GENERATED_DIR}/.stamp
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${TMF_GENERATED_STAGING_DIR}
    COMMAND python3 ${TMF_CODEGEN_SCRIPT} ${TMF640_ARGS} --out ${TMF_GENERATED_STAGING_DIR}
    COMMAND python3 ${TMF_CODEGEN_SYNC} ${TMF_GENERATED_STAGING_DIR} ${TMF_GENERATED_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${TMF_GENERATED_DIR}/.stamp
    DEPENDS ${TMF_CODEGEN_TOOL_SOURCES} ${TMF_CODEGEN_SPECS_DIR}/${TMF640_FILE} ${TMF_CODEGEN_SYNC}
    COMMENT "Regenerating TM Forum DTOs from specs/tmforum (tools/sbi-codegen)"
    VERBATIM
)
add_custom_target(tmf_codegen_run DEPENDS ${TMF_GENERATED_DIR}/.stamp)

add_library(tmf_generated STATIC ${TMF_GENERATED_SOURCES})
add_dependencies(tmf_generated tmf_codegen_run)
target_include_directories(tmf_generated PUBLIC ${TMF_GENERATED_DIR})
target_link_libraries(tmf_generated PUBLIC sbi_core)
target_compile_features(tmf_generated PUBLIC cxx_std_23)
target_compile_options(tmf_generated PRIVATE -w)
