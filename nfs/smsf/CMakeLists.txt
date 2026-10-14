add_executable(smsf src/main.cpp src/stores.cpp src/sms_procedures.cpp src/mt_sms.cpp src/n1n2_sender.cpp src/li_poi.cpp)
target_link_libraries(smsf PRIVATE sbi_core sbi_generated nf_config sms_core li_core li_poi)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
# CONFIG_DIR: docs/DECISIONS.md ADR-0077 -- separate checked-in config/smsf.json.
target_compile_definitions(smsf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs" CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
