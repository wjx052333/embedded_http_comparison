cmake_minimum_required(VERSION 3.10)
project(http-client)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenSSL (commonly available, used by libdatachannel)
find_package(OpenSSL REQUIRED)

# Optional: use MbedTLS instead if available
# find_package(MbedTLS REQUIRED)

# Add JSON library for parsing OpenAI responses
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found, using header-only version")
    set(JSON_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/nlohmann")
endif()

# HTTP Client Library
add_library(http_client STATIC
    src/http_client.cpp
)

target_include_directories(http_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(http_client PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
)

# OpenAI Client Library
add_library(openai_client STATIC
    src/openai_client.cpp
)

target_include_directories(openai_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(openai_client PUBLIC
    http_client
)

if(nlohmann_json_FOUND)
    target_link_libraries(openai_client PUBLIC nlohmann_json::nlohmann_json)
endif()

# Example executable
add_executable(chat_example examples/chat_example.cpp)

target_link_libraries(chat_example PRIVATE
    openai_client
    http_client
)

# Enable testing
enable_testing()

# Optional: Example tests
add_executable(http_client_test test/http_client_test.cpp)
target_link_libraries(http_client_test PRIVATE http_client)
add_test(NAME http_client_test COMMAND http_client_test)
