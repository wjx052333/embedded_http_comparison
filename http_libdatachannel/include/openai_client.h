#pragma once

#include "http_client.h"
#include <functional>
#include <string>
#include <vector>
#include <map>

namespace openai {

struct Message {
    std::string role;  // "system", "user", "assistant"
    std::string content;
};

struct ChatCompletionChoice {
    int index = 0;
    Message delta;  // For streaming responses
    std::string finish_reason;
};

struct ChatCompletionResponse {
    std::string id;
    std::string object;
    long created = 0;
    std::string model;
    std::vector<ChatCompletionChoice> choices;
    int usage_prompt_tokens = 0;
    int usage_completion_tokens = 0;
};

class OpenAIClient {
public:
    struct Config {
        std::string api_key;
        std::string model = "gpt-3.5-turbo";  // or "gpt-4", etc.
        float temperature = 0.7f;
        int max_tokens = 1000;
        std::string base_url = "https://api.openai.com/v1";

        http::HttpClient::Config http_config;
    };

    using StreamCallback = std::function<void(const ChatCompletionChoice& choice)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    using CompleteCallback = std::function<void(const ChatCompletionResponse& response)>;

    explicit OpenAIClient(const Config& config);
    ~OpenAIClient();

    /**
     * Send chat completion request with streaming response
     * @param messages List of conversation messages
     * @param on_chunk Callback for each streamed token
     * @param on_error Error callback
     * @return true if request started successfully
     */
    bool chat_stream(const std::vector<Message>& messages,
                     StreamCallback on_chunk,
                     ErrorCallback on_error = nullptr);

    /**
     * Send chat completion request and get complete response
     * @param messages List of conversation messages
     * @param on_error Error callback
     * @return Complete chat response
     */
    ChatCompletionResponse chat(const std::vector<Message>& messages,
                                ErrorCallback on_error = nullptr);

    const std::string& get_last_error() const { return mLastError; }

private:
    std::string build_request_body(const std::vector<Message>& messages);
    std::map<std::string, std::string> build_headers();
    ChatCompletionResponse parse_response(const std::string& json_str);
    ChatCompletionChoice parse_stream_chunk(const std::string& json_str);

    Config mConfig;
    std::unique_ptr<http::HttpClient> mHttpClient;
    std::string mLastError;

    void set_error(const std::string& msg);
};

}  // namespace openai
