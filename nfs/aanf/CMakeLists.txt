# AAnF -- AKMA Anchor Function, TS 33.535 / TS 29.535 Naanf_AKMA, increment 1 (ADR-0495).
find_package(redis++ CONFIG REQUIRED)
add_executable(aanf src/main.cpp src/akma_store.cpp)
target_link_libraries(aanf PRIVATE sbi_core sbi_generated aka_crypto nf_config redis++::redis++_static)
# See nfs/hello-nf/CMakeLists.txt for why this is a compile-time absolute path.
target_compile_definitions(aanf PRIVATE CERTS_DIR="${CMAKE_SOURCE_DIR}/certs" CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")
