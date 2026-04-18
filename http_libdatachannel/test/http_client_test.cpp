#include <http_client.h>
#include <iostream>
#include <cassert>

void test_url_parsing() {
    std::cout << "Testing URL parsing..." << std::endl;

    auto url1 = http::HttpClient::Url::parse("https://api.openai.com/v1/chat/completions");
    assert(url1.scheme == "https");
    assert(url1.host == "api.openai.com");
    assert(url1.port == 443);
    assert(url1.path == "/v1/chat/completions");

    auto url2 = http::HttpClient::Url::parse("http://example.com:8080/path?query=value");
    assert(url2.scheme == "http");
    assert(url2.host == "example.com");
    assert(url2.port == 8080);
    assert(url2.path == "/path");
    assert(url2.query == "query=value");

    std::cout << "  ✓ URL parsing tests passed" << std::endl;
}

void test_http_client_creation() {
    std::cout << "Testing HTTP client creation..." << std::endl;

    http::HttpClient::Config config;
    config.connect_timeout_ms = 5000;
    config.read_timeout_ms = 30000;

    http::HttpClient client(config);
    std::cout << "  ✓ HTTP client created successfully" << std::endl;
}

int main() {
    try {
        test_url_parsing();
        test_http_client_creation();

        std::cout << "\nAll tests passed! ✓" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
