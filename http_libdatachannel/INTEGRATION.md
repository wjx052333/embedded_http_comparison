# Integration Guide: Using HTTP Client with libdatachannel

This document describes how to integrate the HTTP client library with your existing libdatachannel project.

## Overview

The HTTP client library provides:
- Lightweight HTTPS support for calling OpenAI API
- Server-Sent Events (SSE) streaming response parsing
- Clean C++17 API
- Minimal dependencies (only requires OpenSSL/TLS)

You can use it alongside libdatachannel's WebRTC and WebSocket support.

## Integration Steps

### Option 1: As a Submodule (Recommended)

```bash
cd /path/to/your/project
git submodule add https://github.com/your-repo/http-client.git third_party/http_client
```

Then in your CMakeLists.txt:
```cmake
# Add HTTP client
add_subdirectory(third_party/http_client)

# Link against your target
target_link_libraries(your_target PRIVATE http_client openai_client)
```

### Option 2: Copy into Your Project

```bash
cp -r http_libdatachannel/include /path/to/your/project/include/http_client
cp -r http_libdatachannel/src /path/to/your/project/src/http_client
```

Then create a CMakeLists.txt in your src/http_client directory or add files to your existing one.

### Option 3: Build as Standalone Library

```bash
cd http_libdatachannel
./build.sh

# Headers are in: include/
# Libraries are in: build/
```

Then link to the library in your project.

## Using in Your Code

### Include Headers

```cpp
#include <http_client.h>
#include <openai_client.h>
```

### Basic Usage Example

```cpp
#include <http_client.h>
#include <openai_client.h>
#include <iostream>

void chat_with_openai(const std::string& api_key) {
    // Configure OpenAI client
    openai::OpenAIClient::Config config;
    config.api_key = api_key;
    config.model = "gpt-3.5-turbo";
    config.max_tokens = 1000;

    openai::OpenAIClient client(config);

    // Prepare messages
    std::vector<openai::Message> messages = {
        {"system", "You are a helpful assistant."},
        {"user", "Explain quantum computing in simple terms"}
    };

    // Stream the response
    std::string full_response;
    bool success = client.chat_stream(
        messages,
        [&full_response](const openai::ChatCompletionChoice& choice) {
            full_response += choice.delta.content;
            std::cout << choice.delta.content;
            std::cout.flush();
        },
        [](const std::string& error) {
            std::cerr << "Error: " << error << std::endl;
        }
    );

    if (!success) {
        std::cerr << "Request failed: " << client.get_last_error() << std::endl;
        return;
    }

    std::cout << "\n\nFull response:\n" << full_response << std::endl;
}
```

### Integrating with Existing libdatachannel Code

If you're using libdatachannel for WebRTC/WebSocket:

```cpp
#include <rtc/rtc.hpp>  // libdatachannel
#include <openai_client.h>

// Your DataChannel receives a user message
void on_datachannel_message(std::string message) {
    // Call OpenAI API
    openai::OpenAIClient client(config);
    
    std::vector<openai::Message> messages = {
        {"user", message}
    };

    std::string response;
    client.chat_stream(messages,
        [&response](const openai::ChatCompletionChoice& choice) {
            response += choice.delta.content;
            
            // Send back through DataChannel
            channel->send(response);
        }
    );
}
```

## Configuration for Embedded Systems

### Memory-Constrained Devices

```cpp
// Reduce timeouts and buffer sizes
http::HttpClient::Config config;
config.connect_timeout_ms = 3000;
config.read_timeout_ms = 15000;  // Shorter timeout

openai::OpenAIClient::Config openai_config;
openai_config.max_tokens = 500;  // Smaller responses
openai_config.http_config = config;

openai::OpenAIClient client(openai_config);
```

### Network-Constrained Devices

```cpp
// Increase timeouts for poor connectivity
http::HttpClient::Config config;
config.connect_timeout_ms = 15000;
config.read_timeout_ms = 60000;

openai::OpenAIClient::Config openai_config;
openai_config.http_config = config;
```

### SSL Certificate Configuration

For embedded devices with non-standard certificate stores:

```cpp
http::HttpClient::Config config;
config.verify_ssl = true;
config.ca_cert_path = "/etc/ssl/certs/ca-certificates.crt";

openai::OpenAIClient::Config openai_config;
openai_config.http_config = config;
```

## Building with Your Project

### CMakeLists.txt Integration Example

```cmake
cmake_minimum_required(VERSION 3.10)
project(my_app)

set(CMAKE_CXX_STANDARD 17)

# Find dependencies
find_package(OpenSSL REQUIRED)

# Add HTTP client library
add_subdirectory(http_client)

# Your executable
add_executable(my_app
    src/main.cpp
    src/my_code.cpp
)

# Link libraries
target_link_libraries(my_app PRIVATE
    http_client
    openai_client
    OpenSSL::SSL
    OpenSSL::Crypto
)

# If also using libdatachannel
find_package(datachannel REQUIRED)
target_link_libraries(my_app PRIVATE datachannel)
```

## Cross-Compilation

When cross-compiling for embedded devices:

```bash
# Set up toolchain
export CC=arm-linux-gnueabihf-gcc
export CXX=arm-linux-gnueabihf-g++
export AR=arm-linux-gnueabihf-ar

# Build with OpenSSL for target architecture
cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake ..
make
```

## Troubleshooting Integration

### Link Errors
```
undefined reference to `http::HttpClient::post_stream(...)`
```
Solution: Ensure http_client is in target_link_libraries

### Missing Headers
```
fatal error: http_client.h: No such file or directory
```
Solution: Add `target_include_directories(your_target PRIVATE path/to/http_client/include)`

### SSL Errors at Runtime
```
SSL handshake failed
```
Solutions:
1. Check CA certificate path
2. Verify system time is correct (affects SSL validation)
3. Check OpenSSL installation: `openssl version`

## Performance Tips

1. **Reuse Client Instance**: Don't create a new OpenAIClient for each request
   ```cpp
   // Good
   openai::OpenAIClient client(config);
   client.chat_stream(messages1, callback1);
   client.chat_stream(messages2, callback2);
   
   // Avoid
   for (auto& msg : messages) {
       openai::OpenAIClient temp_client(config);
       // ...
   }
   ```

2. **Use Streaming for Large Responses**: Don't wait for complete response
   ```cpp
   // Good - stream callback processes data as it arrives
   client.chat_stream(messages, on_chunk_callback);
   
   // Less efficient - waits for complete response
   auto response = client.chat(messages);
   ```

3. **Tune Timeouts**: Balance responsiveness with reliability
   ```cpp
   config.connect_timeout_ms = 5000;   // 5s for connection
   config.read_timeout_ms = 30000;     // 30s for reading data
   ```

## Thread Safety

The HTTP client is **NOT thread-safe**. For multi-threaded applications:

```cpp
#include <mutex>

class ThreadSafeOpenAI {
    std::mutex mMutex;
    openai::OpenAIClient mClient;
    
public:
    void chat_stream(const std::vector<openai::Message>& messages,
                     openai::StreamCallback cb) {
        std::lock_guard<std::mutex> lock(mMutex);
        mClient.chat_stream(messages, cb);
    }
};
```

## Additional Resources

- [HTTP Client API Reference](./README.md)
- [OpenAI API Documentation](https://platform.openai.com/docs/api-reference)
- [Server-Sent Events Specification](https://html.spec.whatwg.org/multipage/server-sent-events.html)

## Getting Help

For issues or questions:
1. Check the [README.md](./README.md) for usage examples
2. Review the header files for available APIs
3. Check example code in `examples/`
4. Enable debug output: `SSL_CTX_set_info_callback(...)`
