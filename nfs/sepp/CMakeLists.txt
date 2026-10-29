# SEPP -- TS 29.573 N32-c / N32-f, increment 1: TLS security (ADR-0491). See src/main.cpp.
add_library(sepp_n32 STATIC src/n32.cpp src/prins_codec.cpp src/trust_anchors.cpp)
target_include_directories(sepp_n32 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(OpenSSL REQUIRED)
target_link_libraries(sepp_n32 PUBLIC sbi_generated OpenSSL::Crypto)

add_executable(sepp src/main.cpp)
target_link_libraries(sepp PRIVATE sepp_n32 sbi_core sbi_generated nf_config)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(sepp PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs" CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
