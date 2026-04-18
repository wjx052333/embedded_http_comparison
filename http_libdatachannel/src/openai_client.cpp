#include "openai_client.h"
#include <sstream>
#include <regex>

namespace openai {

// Simple JSON escape function
static std::string escape_json_string(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 32) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    result += buf;
                } else {
                    result += c;
                }
        }
    }
    return result;
}

// Simple JSON unescape function
static std::string unescape_json_string(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            char next = str[++i];
            switch (next) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    // Unicode escape
                    if (i + 4 < str.length()) {
                        std::string hex = str.substr(i + 1, 4);
                        int code = std::stoi(hex, nullptr, 16);
                        result += static_cast<char>(code);
                        i += 4;
                    }
                    break;
                }
                default: result += next;
            }
        } else {
            result += str[i];
        }
    }
    return result;
}

// Simple JSON value extraction
static std::string extract_json_value(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"\\s*:\\s*";

    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return "";

    size_t value_start = json.find(':', key_pos);
    if (value_start == std::string::npos) return "";

    value_start = json.find_first_not_of(" \t\n", value_start + 1);
    if (value_start == std::string::npos) return "";

    // Check if value is a string
    if (json[value_start] == '"') {
        size_t value_end = value_start + 1;
        while (value_end < json.length()) {
            if (json[value_end] == '"' && json[value_end - 1] != '\\') {
                return json.substr(value_start + 1, value_end - value_start - 1);
            }
            value_end++;
        }
    } else if (json[value_start] == '[') {
        // Array
        int depth = 1;
        size_t value_end = value_start + 1;
        while (value_end < json.length() && depth > 0) {
            if (json[value_end] == '[') depth++;
            else if (json[value_end] == ']') depth--;
            value_end++;
        }
        return json.substr(value_start, value_end - value_start);
    } else if (json[value_start] == '{') {
        // Object
        int depth = 1;
        size_t value_end = value_start + 1;
        while (value_end < json.length() && depth > 0) {
            if (json[value_end] == '{') depth++;
            else if (json[value_end] == '}') depth--;
            value_end++;
        }
        return json.substr(value_start, value_end - value_start);
    } else {
        // Number, boolean, null
        size_t value_end = value_start;
        while (value_end < json.length() && json[value_end] != ',' && json[value_end] != '}' && json[value_end] != ']') {
            value_end++;
        }
        return json.substr(value_start, value_end - value_start);
    }

    return "";
}

OpenAIClient::OpenAIClient(const Config& config)
    : mConfig(config) {
    mHttpClient = std::make_unique<http::HttpClient>(config.http_config);
}

OpenAIClient::~OpenAIClient() = default;

std::string OpenAIClient::build_request_body(const std::vector<Message>& messages) {
    std::ostringstream json;

    json << "{"
         << "\"model\":\"" << escape_json_string(mConfig.model) << "\","
         << "\"messages\":[";

    for (size_t i = 0; i < messages.size(); ++i) {
        if (i > 0) json << ",";
        json << "{"
             << "\"role\":\"" << escape_json_string(messages[i].role) << "\","
             << "\"content\":\"" << escape_json_string(messages[i].content) << "\""
             << "}";
    }

    json << "],"
         << "\"temperature\":" << mConfig.temperature << ","
         << "\"max_tokens\":" << mConfig.max_tokens << ","
         << "\"stream\":true"
         << "}";

    return json.str();
}

std::map<std::string, std::string> OpenAIClient::build_headers() {
    return {
        {"Content-Type", "application/json"},
        {"Authorization", "Bearer " + mConfig.api_key},
    };
}

ChatCompletionChoice OpenAIClient::parse_stream_chunk(const std::string& json_str) {
    ChatCompletionChoice choice;

    // Parse: {"choices":[{"delta":{"role":"assistant","content":"..."},"finish_reason":null}]}
    std::string choices_str = extract_json_value(json_str, "choices");
    if (choices_str.empty()) return choice;

    // Extract first choice object
    size_t first_choice = choices_str.find('{');
    if (first_choice == std::string::npos) return choice;

    size_t choice_end = choices_str.find('}', first_choice);
    if (choice_end == std::string::npos) return choice;

    std::string choice_obj = choices_str.substr(first_choice, choice_end - first_choice + 1);

    // Extract delta object
    std::string delta_str = extract_json_value(choice_obj, "delta");
    if (!delta_str.empty() && delta_str[0] == '{') {
        choice.delta.role = unescape_json_string(extract_json_value(delta_str, "role"));
        choice.delta.content = unescape_json_string(extract_json_value(delta_str, "content"));
    }

    // Extract finish_reason
    std::string finish_reason = extract_json_value(choice_obj, "finish_reason");
    if (finish_reason != "null" && !finish_reason.empty()) {
        choice.finish_reason = unescape_json_string(finish_reason);
    }

    return choice;
}

ChatCompletionResponse OpenAIClient::parse_response(const std::string& json_str) {
    ChatCompletionResponse response;

    response.id = unescape_json_string(extract_json_value(json_str, "id"));
    response.object = unescape_json_string(extract_json_value(json_str, "object"));

    // Parse created timestamp
    std::string created_str = extract_json_value(json_str, "created");
    if (!created_str.empty()) {
        response.created = std::stol(created_str);
    }

    response.model = unescape_json_string(extract_json_value(json_str, "model"));

    // Parse choices
    std::string choices_str = extract_json_value(json_str, "choices");
    if (!choices_str.empty() && choices_str[0] == '[') {
        size_t pos = 1;
        int choice_idx = 0;
        while (pos < choices_str.length()) {
            size_t choice_start = choices_str.find('{', pos);
            if (choice_start == std::string::npos) break;

            size_t choice_end = choices_str.find('}', choice_start);
            if (choice_end == std::string::npos) break;

            std::string choice_obj = choices_str.substr(choice_start, choice_end - choice_start + 1);

            ChatCompletionChoice choice = parse_stream_chunk(choice_obj);
            choice.index = choice_idx++;
            response.choices.push_back(choice);

            pos = choice_end + 1;
        }
    }

    return response;
}

bool OpenAIClient::chat_stream(const std::vector<Message>& messages,
                               StreamCallback on_chunk,
                               ErrorCallback on_error) {
    std::string body = build_request_body(messages);
    auto headers = build_headers();
    std::string url = mConfig.base_url + "/chat/completions";

    bool result = mHttpClient->post_stream(
        url,
        body,
        headers,
        [this, on_chunk](const std::string& chunk) {
            ChatCompletionChoice choice = parse_stream_chunk(chunk);
            on_chunk(choice);
        },
        on_error
    );

    return result;
}

ChatCompletionResponse OpenAIClient::chat(const std::vector<Message>& messages,
                                          ErrorCallback on_error) {
    ChatCompletionResponse response;

    std::string body = build_request_body(messages);
    auto headers = build_headers();
    std::string url = mConfig.base_url + "/chat/completions";

    // Remove stream flag for non-streaming request
    body = std::regex_replace(body, std::regex(",\"stream\":true"), "");

    auto http_response = mHttpClient->post(url, body, headers);

    if (!http_response.is_success()) {
        std::string error = "HTTP " + std::to_string(http_response.status_code) + ": " + http_response.body;
        set_error(error);
        if (on_error) on_error(error);
        return response;
    }

    response = parse_response(http_response.body);

    return response;
}

void OpenAIClient::set_error(const std::string& msg) {
    mLastError = msg;
}

}  // namespace openai
