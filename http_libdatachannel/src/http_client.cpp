#include "http_client.h"

#include <sstream>
#include <regex>
#include <cstring>
#include <cerrno>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
#else
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace http {

// ============ URL Parsing ============

HttpClient::Url HttpClient::Url::parse(const std::string& url_str) {
    Url url;

    // Parse: scheme://host:port/path?query
    std::regex url_regex(
        R"(^(https?):\/\/([^:\/\?]+)(?::(\d+))?([^\?]*)(?:\?(.*))?$)"
    );

    std::smatch match;
    if (!std::regex_match(url_str, match, url_regex)) {
        throw std::invalid_argument("Invalid URL: " + url_str);
    }

    url.scheme = match[1].str();
    url.host = match[2].str();
    url.port = match[3].matched ? std::stoi(match[3].str()) :
               (url.scheme == "https" ? 443 : 80);
    url.path = match[4].matched ? match[4].str() : "/";
    url.query = match[5].matched ? match[5].str() : "";

    if (url.path.empty()) {
        url.path = "/";
    }

    return url;
}

// ============ HttpClient Implementation ============

HttpClient::HttpClient()
    : mReadBuffer(8192) {
    SSL_library_init();
    SSL_load_error_strings();
}

HttpClient::HttpClient(const Config& config)
    : mConfig(config), mReadBuffer(8192) {
    SSL_library_init();
    SSL_load_error_strings();
}

HttpClient::~HttpClient() {
    close();
}

bool HttpClient::connect(const std::string& host, int port) {
    if (mSocket != -1) {
        disconnect();
    }

    // Create socket
    mSocket = ::socket(AF_INET, SOCK_STREAM, 0);
    if (mSocket < 0) {
        set_error("Failed to create socket");
        return false;
    }

    // Set timeout
#ifdef _WIN32
    DWORD timeout = mConfig.connect_timeout_ms;
    setsockopt(mSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    setsockopt(mSocket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = mConfig.connect_timeout_ms / 1000;
    tv.tv_usec = (mConfig.connect_timeout_ms % 1000) * 1000;
    setsockopt(mSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(mSocket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    // Resolve hostname
    struct addrinfo hints = {}, *results = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int ret = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &results);
    if (ret != 0) {
        set_error(std::string("DNS resolution failed: ") + gai_strerror(ret));
        ::close(mSocket);
        mSocket = -1;
        return false;
    }

    // Connect to server
    ret = ::connect(mSocket, results->ai_addr, results->ai_addrlen);
    freeaddrinfo(results);

    if (ret < 0) {
        set_error("Failed to connect to server: " + std::string(strerror(errno)));
        ::close(mSocket);
        mSocket = -1;
        return false;
    }

    mConnected = true;
    return true;
}

void HttpClient::disconnect() {
    if (mSSLConnection) {
        SSL_shutdown(static_cast<SSL*>(mSSLConnection));
        SSL_free(static_cast<SSL*>(mSSLConnection));
        mSSLConnection = nullptr;
    }

    if (mSSLContext) {
        SSL_CTX_free(static_cast<SSL_CTX*>(mSSLContext));
        mSSLContext = nullptr;
    }

    if (mSocket != -1) {
        ::close(mSocket);
        mSocket = -1;
    }

    mConnected = false;
    mReadBufferPos = 0;
}

void HttpClient::close() {
    disconnect();
}

bool HttpClient::send_request(const std::string& method,
                             const std::string& path,
                             const std::string& body,
                             const std::map<std::string, std::string>& headers) {
    std::ostringstream req;

    // Build HTTP request line
    req << method << " " << path << " HTTP/1.1\r\n";
    req << "Host: " << mCurrentUrl.host << "\r\n";
    req << "Connection: close\r\n";
    req << "User-Agent: http-client/1.0\r\n";

    // Add custom headers
    for (const auto& [key, value] : headers) {
        req << key << ": " << value << "\r\n";
    }

    // Add content length if body exists
    if (!body.empty()) {
        req << "Content-Length: " << body.length() << "\r\n";
    }

    req << "\r\n";
    if (!body.empty()) {
        req << body;
    }

    std::string request = req.str();

    // Send request
    if (mSSLConnection) {
        SSL* ssl = static_cast<SSL*>(mSSLConnection);
        int written = SSL_write(ssl, request.data(), request.length());
        if (written <= 0) {
            set_error("SSL_write failed");
            return false;
        }
    } else {
        int written = ::send(mSocket, request.data(), request.length(), 0);
        if (written < 0) {
            set_error("send failed: " + std::string(strerror(errno)));
            return false;
        }
    }

    return true;
}

std::string HttpClient::read_line() {
    std::string line;

    while (true) {
        // Check if we have data in buffer
        if (mReadBufferPos < mReadBuffer.size()) {
            char c = mReadBuffer[mReadBufferPos++];
            if (c == '\n') {
                // Remove \r if present
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                return line;
            }
            line += c;
            continue;
        }

        // Need to read more data
        mReadBufferPos = 0;
        int nread;

        if (mSSLConnection) {
            SSL* ssl = static_cast<SSL*>(mSSLConnection);
            nread = SSL_read(ssl, mReadBuffer.data(), mReadBuffer.size());
            if (nread <= 0) {
                return line;  // EOF or error
            }
        } else {
            nread = ::recv(mSocket, mReadBuffer.data(), mReadBuffer.size(), 0);
            if (nread <= 0) {
                return line;
            }
        }

        mReadBuffer.resize(nread);
    }
}

std::string HttpClient::read_until(const std::string& delimiter) {
    std::string data;

    while (true) {
        std::string line = read_line();
        if (line.empty() && mReadBufferPos >= mReadBuffer.size()) {
            break;  // EOF
        }

        data += line + "\n";

        if (data.find(delimiter) != std::string::npos) {
            break;
        }
    }

    return data;
}

HttpResponse HttpClient::read_response() {
    HttpResponse response;

    // Read status line
    std::string status_line = read_line();
    if (status_line.empty()) {
        set_error("Empty response from server");
        return response;
    }

    // Parse status code
    std::regex status_regex(R"(HTTP/\d\.\d\s+(\d+))");
    std::smatch match;
    if (std::regex_search(status_line, match, status_regex)) {
        response.status_code = std::stoi(match[1].str());
    }

    // Read headers
    while (true) {
        std::string header_line = read_line();
        if (header_line.empty()) {
            break;  // End of headers
        }

        size_t colon_pos = header_line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = header_line.substr(0, colon_pos);
            std::string value = header_line.substr(colon_pos + 1);

            // Trim leading whitespace
            value.erase(0, value.find_first_not_of(" \t"));

            response.headers[key] = value;
        }
    }

    // Read body
    while (true) {
        char buffer[4096];
        int nread;

        if (mSSLConnection) {
            SSL* ssl = static_cast<SSL*>(mSSLConnection);
            nread = SSL_read(ssl, buffer, sizeof(buffer));
        } else {
            nread = ::recv(mSocket, buffer, sizeof(buffer), 0);
        }

        if (nread <= 0) break;
        response.body.append(buffer, nread);
    }

    return response;
}

void HttpClient::process_sse_stream(ResponseCallback on_chunk, ErrorCallback on_error) {
    std::string buffer;

    while (true) {
        std::string line = read_line();
        if (line.empty() && mReadBufferPos >= mReadBuffer.size()) {
            break;  // EOF
        }

        buffer += line + "\n";

        // SSE format: "data: {json}\n\n"
        if (line.empty()) {
            // Empty line indicates end of an event
            if (buffer.find("data:") != std::string::npos) {
                // Extract data part
                std::regex data_regex(R"(^data:\s*(.*)$)", std::regex::multiline);
                std::smatch match;
                std::string::const_iterator search_start(buffer.cbegin());

                while (std::regex_search(search_start, buffer.cend(), match, data_regex)) {
                    std::string data = match[1].str();

                    // Handle [DONE] marker
                    if (data != "[DONE]") {
                        on_chunk(data);
                    }

                    search_start = match.suffix().first;
                }
            }
            buffer.clear();
        }
    }
}

HttpResponse HttpClient::post(const std::string& url,
                              const std::string& body,
                              const std::map<std::string, std::string>& headers) {
    HttpResponse response;

    try {
        mCurrentUrl = Url::parse(url);
    } catch (const std::exception& e) {
        set_error(e.what());
        return response;
    }

    // Check if HTTPS
    if (mCurrentUrl.scheme == "https") {
        if (!connect(mCurrentUrl.host, mCurrentUrl.port)) {
            return response;
        }

        // Setup SSL/TLS
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            set_error("Failed to create SSL context");
            disconnect();
            return response;
        }

        mSSLContext = ctx;

        // Load CA certificates
        if (!mConfig.ca_cert_path.empty()) {
            if (!SSL_CTX_load_verify_locations(ctx, mConfig.ca_cert_path.c_str(), nullptr)) {
                SSL_CTX_free(ctx);
                set_error("Failed to load CA certificates");
                disconnect();
                return response;
            }
        } else {
            SSL_CTX_set_default_verify_paths(ctx);
        }

        // Create SSL connection
        SSL* ssl = SSL_new(ctx);
        if (!ssl) {
            set_error("Failed to create SSL connection");
            disconnect();
            return response;
        }

        mSSLConnection = ssl;
        SSL_set_fd(ssl, mSocket);
        SSL_set_tlsext_host_name(ssl, mCurrentUrl.host.c_str());

        // Perform SSL handshake
        if (SSL_connect(ssl) <= 0) {
            set_error("SSL handshake failed");
            disconnect();
            return response;
        }
    } else {
        // HTTP connection
        if (!connect(mCurrentUrl.host, mCurrentUrl.port)) {
            return response;
        }
    }

    // Send request
    if (!send_request("POST", mCurrentUrl.path, body, headers)) {
        disconnect();
        return response;
    }

    // Read response
    response = read_response();
    disconnect();

    return response;
}

bool HttpClient::post_stream(const std::string& url,
                             const std::string& body,
                             const std::map<std::string, std::string>& headers,
                             ResponseCallback on_chunk,
                             ErrorCallback on_error) {
    try {
        mCurrentUrl = Url::parse(url);
    } catch (const std::exception& e) {
        if (on_error) on_error(e.what());
        return false;
    }

    // Check if HTTPS
    if (mCurrentUrl.scheme == "https") {
        if (!connect(mCurrentUrl.host, mCurrentUrl.port)) {
            if (on_error) on_error(mLastError);
            return false;
        }

        // Setup SSL/TLS
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            if (on_error) on_error("Failed to create SSL context");
            disconnect();
            return false;
        }

        mSSLContext = ctx;

        // Load CA certificates
        if (!mConfig.ca_cert_path.empty()) {
            if (!SSL_CTX_load_verify_locations(ctx, mConfig.ca_cert_path.c_str(), nullptr)) {
                SSL_CTX_free(ctx);
                if (on_error) on_error("Failed to load CA certificates");
                disconnect();
                return false;
            }
        } else {
            SSL_CTX_set_default_verify_paths(ctx);
        }

        // Create SSL connection
        SSL* ssl = SSL_new(ctx);
        if (!ssl) {
            if (on_error) on_error("Failed to create SSL connection");
            disconnect();
            return false;
        }

        mSSLConnection = ssl;
        SSL_set_fd(ssl, mSocket);
        SSL_set_tlsext_host_name(ssl, mCurrentUrl.host.c_str());

        // Perform SSL handshake
        if (SSL_connect(ssl) <= 0) {
            if (on_error) on_error("SSL handshake failed");
            disconnect();
            return false;
        }
    } else {
        // HTTP connection
        if (!connect(mCurrentUrl.host, mCurrentUrl.port)) {
            if (on_error) on_error(mLastError);
            return false;
        }
    }

    // Send request
    if (!send_request("POST", mCurrentUrl.path, body, headers)) {
        if (on_error) on_error(mLastError);
        disconnect();
        return false;
    }

    // Skip HTTP response headers and process SSE stream
    while (true) {
        std::string header_line = read_line();
        if (header_line.empty()) {
            break;  // End of headers, start of body
        }
    }

    // Process streaming response
    process_sse_stream(on_chunk, on_error);
    disconnect();

    return true;
}

HttpResponse HttpClient::get(const std::string& url,
                             const std::map<std::string, std::string>& headers) {
    HttpResponse response;

    try {
        mCurrentUrl = Url::parse(url);
    } catch (const std::exception& e) {
        set_error(e.what());
        return response;
    }

    // Check if HTTPS
    if (mCurrentUrl.scheme == "https") {
        if (!connect(mCurrentUrl.host, mCurrentUrl.port)) {
            return response;
        }

        // Setup SSL/TLS
        SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            set_error("Failed to create SSL context");
            disconnect();
            return response;
        }

        mSSLContext = ctx;

        // Load CA certificates
        if (!mConfig.ca_cert_path.empty()) {
            if (!SSL_CTX_load_verify_locations(ctx, mConfig.ca_cert_path.c_str(), nullptr)) {
                SSL_CTX_free(ctx);
                set_error("Failed to load CA certificates");
                disconnect();
                return response;
            }
        } else {
            SSL_CTX_set_default_verify_paths(ctx);
        }

        // Create SSL connection
        SSL* ssl = SSL_new(ctx);
        if (!ssl) {
            set_error("Failed to create SSL connection");
            disconnect();
            return response;
        }

        mSSLConnection = ssl;
        SSL_set_fd(ssl, mSocket);
        SSL_set_tlsext_host_name(ssl, mCurrentUrl.host.c_str());

        // Perform SSL handshake
        if (SSL_connect(ssl) <= 0) {
            set_error("SSL handshake failed");
            disconnect();
            return response;
        }
    } else {
        // HTTP connection
        if (!connect(mCurrentUrl.host, mCurrentUrl.port)) {
            return response;
        }
    }

    // Send GET request
    if (!send_request("GET", mCurrentUrl.path, "", headers)) {
        disconnect();
        return response;
    }

    // Read response
    response = read_response();
    disconnect();

    return response;
}

void HttpClient::set_error(const std::string& msg) {
    mLastError = msg;
}

}  // namespace http
