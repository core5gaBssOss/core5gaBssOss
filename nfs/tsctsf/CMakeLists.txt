# TSCTSF -- TS 29.565 Ntsctsf_QoSandTSCAssistance, increment 1 (ADR-0498). See src/main.cpp.
add_executable(tsctsf src/main.cpp)
target_link_libraries(tsctsf PRIVATE sbi_core sbi_generated nf_config)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(tsctsf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs" CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
