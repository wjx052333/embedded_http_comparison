#include <openai_client.h>
#include <iostream>
#include <string>

int main() {
    // Configure OpenAI client
    openai::OpenAIClient::Config config;
    config.api_key = std::getenv("OPENAI_API_KEY") ? std::getenv("OPENAI_API_KEY") : "";
    config.model = "gpt-3.5-turbo";
    config.temperature = 0.7f;
    config.max_tokens = 1000;

    if (config.api_key.empty()) {
        std::cerr << "Error: OPENAI_API_KEY environment variable not set" << std::endl;
        return 1;
    }

    openai::OpenAIClient client(config);

    // Prepare messages
    std::vector<openai::Message> messages = {
        {"system", "You are a helpful assistant."},
        {"user", "Hello! What is the capital of France?"}
    };

    std::cout << "Sending chat request to OpenAI API..." << std::endl;
    std::cout << "User: What is the capital of France?" << std::endl;
    std::cout << "\nAssistant: ";
    std::cout.flush();

    // Send streaming chat completion request
    bool success = client.chat_stream(
        messages,
        [](const openai::ChatCompletionChoice& choice) {
            // Callback for each streamed token
            if (!choice.delta.content.empty()) {
                std::cout << choice.delta.content;
                std::cout.flush();
            }
        },
        [](const std::string& error) {
            // Error callback
            std::cerr << "\nError: " << error << std::endl;
        }
    );

    if (!success) {
        std::cerr << "Failed to send request: " << client.get_last_error() << std::endl;
        return 1;
    }

    std::cout << "\n\nDone!" << std::endl;

    // Example of non-streaming request
    std::cout << "\n--- Non-streaming request ---" << std::endl;
    messages.push_back({"assistant", "The capital of France is Paris."});
    messages.push_back({"user", "What about Germany?"});

    auto response = client.chat(messages,
        [](const std::string& error) {
            std::cerr << "Error: " << error << std::endl;
        }
    );

    std::cout << "Response ID: " << response.id << std::endl;
    if (!response.choices.empty()) {
        std::cout << "Answer: " << response.choices[0].delta.content << std::endl;
    }

    return 0;
}
