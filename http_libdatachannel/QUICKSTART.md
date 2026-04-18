# Quick Start Guide

Get the HTTP Client Library working in 5 minutes.

## Prerequisites

- Linux/macOS/Windows
- CMake 3.10+
- OpenSSL development libraries
- C++17 compatible compiler

## 1. Install Dependencies

### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install -y cmake libssl-dev g++ make
```

### macOS
```bash
brew install cmake openssl
```

### Windows
Download and install:
- [CMake](https://cmake.org/download/)
- [OpenSSL](https://slproweb.com/products/Win32OpenSSL.html) or [vcpkg](https://github.com/Microsoft/vcpkg)

## 2. Build the Library

```bash
# Navigate to project directory
cd /home/wjx/agent_eyes/bot/mclaw/http/http_libdatachannel

# Create and enter build directory
mkdir -p build && cd build

# Configure with CMake
cmake ..

# Build
make -j$(nproc)

# Run tests (optional)
make test
```

## 3. Run the Example

First, set your OpenAI API key:

```bash
export OPENAI_API_KEY="sk-..."
```

Then run the example:

```bash
./examples/chat_example
```

Expected output:
```
Sending chat request to OpenAI API...
User: What is the capital of France?