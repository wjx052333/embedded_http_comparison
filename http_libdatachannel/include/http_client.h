#pragma once

#include <string>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace http {

using ResponseCallback = std::function<void(const std::string& data)>;
using ErrorCallback = std::function<void(const std::string& error)>;

class HttpResponse {
public:
    int status_code = 0;
    std::map<std::string, std::string> headers;
    std::string body;

    bool is_success() const { return status_code >= 200 && status_code < 300; }
};

class HttpClient {
public:
    struct Config {
        int connect_timeout_ms = 5000;
        int read_timeout_ms = 30000;
        bool verify_ssl = true;
        std::string ca_cert_path;  // Path to CA certificate file
    };

    explicit HttpClient();
    explicit HttpClient(const Config& config);
    ~HttpClient();

    /**
     * Send POST request with streaming response (Server-Sent Events)
     * @param url Full URL (https://api.openai.com/v1/chat/completions)
     * @param body Request body (JSON)
     * @param headers HTTP headers
     * @param on_chunk Callback for each SSE event
     * @param on_error Error callback
     * @return true if connection succeeded and started streaming
     */
    bool post_stream(const std::string& url,
                     const std::string& body,
                     const std::map<std::string, std::string>& headers,
                     ResponseCallback on_chunk,
                     ErrorCallback on_error = nullptr);

    /**
     * Send POST request and get complete response
     * @param url Full URL
     * @param body Request body
     * @param headers HTTP headers
     * @return Response object
     */
    HttpResponse post(const std::string& url,
                      const std::string& body,
                      const std::map<std::string, std::string>& headers);

    /**
     * Send GET request
     * @param url Full URL
     * @param headers HTTP headers
     * @return Response object
     */
    HttpResponse get(const std::string& url,
                     const std::map<std::string, std::string>& headers = {});

    void close();

    struct Url {
        std::string scheme;
        std::string host;
        int port = 443;
        std::string path;
        std::string query;

        static Url parse(const std::string& url_str);
    };

private:

    bool connect(const std::string& host, int port);
    void disconnect();

    bool send_request(const std::string& method,
                      const std::string& path,
                      const std::string& body,
                      const std::map<std::string, std::string>& headers);

    HttpResponse read_response();
    void process_sse_stream(ResponseCallback on_chunk, ErrorCallback on_error);

    std::string read_line();
    std::string read_until(const std::string& delimiter);

    Config mConfig;
    int mSocket = -1;
    void* mSSLContext = nullptr;  // SSL_CTX*
    void* mSSLConnection = nullptr;  // SSL*

    bool mConnected = false;
    Url mCurrentUrl;

    // Buffer for reading
    std::vector<char> mReadBuffer;
    size_t mReadBufferPos = 0;

    void set_error(const std::string& msg);
    std::string mLastError;
};

}  // namespace http
