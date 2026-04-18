#!/bin/bash

# Build script for http-client library

set -e

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}Building http-client library...${NC}"

# Create build directory
mkdir -p build
cd build

# Configure
echo -e "${BLUE}Running CMake...${NC}"
cmake ..

# Build
echo -e "${BLUE}Building...${NC}"
make -j$(nproc)

echo -e "${GREEN}Build completed successfully!${NC}"
echo ""
echo -e "${BLUE}Available targets:${NC}"
echo "  - http_client: Core HTTP client library"
echo "  - openai_client: OpenAI client library"
echo "  - chat_example: Chat completion example"
echo "  - http_client_test: Unit tests"
echo ""
echo -e "${BLUE}To run tests:${NC}"
echo "  make test"
echo ""
echo -e "${BLUE}To run example:${NC}"
echo "  ./examples/chat_example"
echo ""
echo -e "${GREEN}Done!${NC}"
