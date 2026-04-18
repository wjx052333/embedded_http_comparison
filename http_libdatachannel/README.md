# HTTP Client Library for OpenAI API

A lightweight, embeddable HTTP/HTTPS client library for calling OpenAI's Chat Completion API with streaming support (SSE).

**Features:**
- ✅ HTTPS support using OpenSSL
- ✅ Server-Sent Events (SSE) streaming responses
- ✅ OpenAI Chat Completion API client
- ✅ No external dependencies (except OpenSSL)
- ✅ Designed for embedded systems
- ✅ Minimal memory footprint

## Project Structure

```
http_libdatachannel/
├── CMakeLists.txt                 # Build configuration
├── README.md                       # This file
├── include/
│   ├── http_client.h             # Core HTTP client
│   └── openai_client.h           # OpenAI-specific client
├── src/
│   ├── http_client.cpp           # HTTP client implementation
│   └── openai_client.cpp         # OpenAI client implementation
├── examples/
│   └── chat_example.cpp          # Example usage
└── test/
    └── http_client_test.cpp      # Unit tests
```

## Dependencies

- **CMake** >= 3.10
- **OpenSSL** (or MbedTLS/GnuTLS)
- **C++17** compiler

### Ubuntu/Debian
```bash
sudo apt-get install cmake libssl-dev
```

### macOS
```bash
brew install cmake openssl
```

### Windows
Download OpenSSL from https://slproweb.com/products/Win32OpenSSL.html

## Building

### Basic Build
```bash
cd /path/to/http_libdatachannel
mkdir build
cd build
cmake ..
make
```

### Build with Tests
```bash
cmake ..
make
./test/http_client_test
```

### Build Example
```bash
cmake ..
make
./examples/chat_example
```

## Usage

### Basic Example - Streaming Chat Completion

```cpp
#include <openai_client.h>
#include <iostream>

int main() {
    // Configure client
    openai::OpenAIClient::Config config;
    config.api_key = "your-openai-api-key";
    config.model = "gpt-3.5-turbo";
    config.temperature = 0.7f;
    config.max_tokens = 2000;

    openai::OpenAIClient client(config);

    // Prepare conversation
    std::vector<openai::Message> messages = {
        {"system", "You are a helpful assistant."},
        {"user", "What is 2+2?"}
    };

    // Stream response
    std::cout << "Assistant: ";
    bool success = client.chat_stream(
        messages,
        [](const openai::ChatCompletionChoice& choice) {
            // Called for each token received
            std::cout << choice.delta.content;
            std::cout.flush();
        },
        [](const std::string& error) {
            // Error handling
            std::cerr << "Error: " << error << std::endl;
        }
    );

    if (!success) {
        std::cerr << "Failed: " << client.get_last_error() << std::endl;
        return 1;
    }

    std::cout << std::endl;
    return 0;
}
```

### Non-Streaming Chat Completion

```cpp
// ... setup as above ...

// Get complete response at once
auto response = client.chat(messages, nullptr);

if (!response.choices.empty()) {
    std::cout << "Response: " << response.choices[0].delta.content << std::endl;
}
```

### Using HTTP Client Directly

```cpp
#include <http_client.h>
#include <iostream>

int main() {
    http::HttpClient::Config config;
    config.connect_timeout_ms = 5000;
    config.verify_ssl = true;

    http::HttpClient client(config);

    // POST request with streaming response
    client.post_stream(
        "https://api.openai.com/v1/chat/completions",
        R"({"model":"gpt-3.5-turbo","messages":[{"role":"user","content":"Hello"}],"stream":true})",
        {
            {"Authorization", "Bearer sk-..."},
            {"Content-Type", "application/json"}
        },
        [](const std::string& chunk) {
            std::cout << chunk;
        },
        [](const std::string& error) {
            std::cerr << "Error: " << error << std::endl;
        }
    );

    return 0;
}
```

## Configuration

### HttpClient::Config

```cpp
struct Config {
    int connect_timeout_ms = 5000;    // Connection timeout
    int read_timeout_ms = 30000;      // Read timeout
    bool verify_ssl = true;           // Verify SSL certificates
    std::string ca_cert_path;         // Path to CA certificate file (optional)
};
```

### OpenAIClient::Config

```cpp
struct Config {
    std::string api_key;              // OpenAI API key (required)
    std::string model = "gpt-3.5-turbo";  // Model to use
    float temperature = 0.7f;         // Temperature (0-2)
    int max_tokens = 1000;            // Max tokens in response
    std::string base_url = "https://api.openai.com/v1";
    
    http::HttpClient::Config http_config;  // HTTP client config
};
```

## API Reference

### OpenAIClient

#### `chat_stream(...)`
Send chat completion request with streaming response (SSE).

```cpp
bool chat_stream(
    const std::vector<Message>& messages,
    StreamCallback on_chunk,
    ErrorCallback on_error = nullptr
);
```

Parameters:
- `messages`: Vector of Message objects (role + content)
- `on_chunk`: Called for each streamed token
- `on_error`: Called on error

Return: `true` if request started successfully

#### `chat(...)`
Send chat completion request and get complete response.

```cpp
ChatCompletionResponse chat(
    const std::vector<Message>& messages,
    ErrorCallback on_error = nullptr
);
```

Parameters:
- `messages`: Vector of Message objects
- `on_error`: Called on error

Return: ChatCompletionResponse object

### HttpClient

#### `post_stream(...)`
Send POST request with streaming response.

```cpp
bool post_stream(
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers,
    ResponseCallback on_chunk,
    ErrorCallback on_error = nullptr
);
```

#### `post(...)`
Send POST request and get complete response.

```cpp
HttpResponse post(
    const std::string& url,
    const std::string& body,
    const std::map<std::string, std::string>& headers
);
```

#### `get(...)`
Send GET request.

```cpp
HttpResponse get(
    const std::string& url,
    const std::map<std::string, std::string>& headers = {}
);
```

## Environment Variables

Set your OpenAI API key:
```bash
export OPENAI_API_KEY="sk-..."
```

## SSL/TLS Configuration

The client uses OpenSSL for HTTPS. To use a custom CA certificate:

```cpp
openai::OpenAIClient::Config config;
config.http_config.ca_cert_path = "/path/to/ca.pem";
config.http_config.verify_ssl = true;
```

## Limitations

- No automatic JSON parsing (manual JSON construction/parsing)
- Single-threaded (each request blocks)
- No automatic retry logic
- No support for form data encoding (JSON only)

## Performance Considerations

For embedded systems:
- Keep messages concise (less data to transfer)
- Use appropriate timeouts
- Consider memory usage with max_tokens parameter
- Use streaming for large responses

## Troubleshooting

### SSL Certificate Verification Failed
```cpp
// Disable verification (not recommended for production)
config.http_config.verify_ssl = false;

// Or provide CA certificate path
config.http_config.ca_cert_path = "/etc/ssl/certs/ca-certificates.crt";
```

### Connection Timeout
Increase timeout values:
```cpp
config.http_config.connect_timeout_ms = 10000;  // 10 seconds
config.http_config.read_timeout_ms = 60000;     // 60 seconds
```

### OpenSSL Not Found During Build
```bash
# Ubuntu/Debian
sudo apt-get install libssl-dev

# macOS
brew install openssl
export LDFLAGS="-L/usr/local/opt/openssl/lib"
export CPPFLAGS="-I/usr/local/opt/openssl/include"
```

## License

Mozilla Public License 2.0 (compatible with libdatachannel)

## Contributing

Contributions welcome! Areas for improvement:
- Async/non-blocking I/O
- Connection pooling
- Automatic retry logic
- Full JSON parsing library integration

## References

- [OpenAI API Documentation](https://platform.openai.com/docs/api-reference)
- [Server-Sent Events (MDN)](https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events)
- [HTTP/1.1 Specification](https://tools.ietf.org/html/rfc7230)
