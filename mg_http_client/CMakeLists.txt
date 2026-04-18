cmake_minimum_required(VERSION 3.10)
project(mg_http_client C)

set(MBEDTLS_ROOT /home/wjx/agent_eyes/webrtc_zlmediakit/mbedtls_pack/install)

# Use mhc.c only; mongoose is taken from the pre-built libmongoose.a (compiled
# with MG_TLS=1 and the same mbedtls config as the linked libraries).
add_library(mhc STATIC mhc.c)
set_target_properties(mhc PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS ON)
target_compile_options(mhc PRIVATE -Wall -Wextra)
target_compile_definitions(mhc PUBLIC MG_TLS=1)  # MG_TLS_MBED=1
target_include_directories(mhc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MBEDTLS_ROOT}/include/mongoose
    ${MBEDTLS_ROOT}/include)
target_link_libraries(mhc
    ${MBEDTLS_ROOT}/lib/libmongoose.a
    ${MBEDTLS_ROOT}/lib/libmbedtls.a
    ${MBEDTLS_ROOT}/lib/libmbedx509.a
    ${MBEDTLS_ROOT}/lib/libmbedcrypto.a)

add_subdirectory(test)
