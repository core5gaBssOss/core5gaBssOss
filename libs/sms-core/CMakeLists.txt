# SMS-over-NAS codec (ADR-0486): the CP (TS 24.011 clause 8.1), RP (TS 24.011 clause 8.2) and TP
# (TS 23.040 clause 9) layers an SMSF reads inside an SMS payload, plus the SMS-SUBMIT-REPORT /
# RP-ACK / RP-ERROR / CP-ACK it sends back.

find_package(tl-expected CONFIG REQUIRED)

add_library(sms_core STATIC
    src/sms.cpp
)

target_include_directories(sms_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(sms_core PUBLIC
    5gc_project_options
    tl::expected
)

target_compile_features(sms_core PUBLIC cxx_std_23)
