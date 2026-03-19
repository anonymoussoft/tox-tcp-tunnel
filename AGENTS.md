# AGENTS.md

This file provides guidance to Codex (Codex.ai/code) when working with code in this repository.

## Build Commands

```bash
# Standard build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Parallel build
cmake --build build -j$(nproc)        # Linux
cmake --build build -j$(sysctl -n hw.ncpu)  # macOS

# Debug build with AddressSanitizer
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DTOXTUNNEL_ENABLE_ASAN=ON
cmake --build build
```

## Running Tests

```bash
# Run all tests via CTest
cd build && ctest --output-on-failure

# Run unit tests directly
./build/tests/unit_tests

# Run integration tests directly
./build/tests/integration_tests

# Run specific test with Google Test filter
./build/tests/unit_tests --gtest_filter=ConfigTest*
./build/tests/integration_tests --gtest_filter=TunnelDataFlowTest*
```

## Code Style

- Google style with 4-space indentation, 100-character column limit
- Run `clang-format` before committing
- Compiler warnings are treated as errors (`-Werror`)
- C++20 standard

## Architecture

ToxTunnel forwards TCP ports through the Tox P2P network with end-to-end encryption.

```
CLI/Config Layer → Application Layer (TunnelServer/TunnelClient/RulesEngine)
                              ↓
              TCP I/O Layer (asio)  |  Tox Layer (dedicated thread)
```

### Key Components

| Layer | Components |
|-------|------------|
| Application | `TunnelServer`, `TunnelClient`, `RulesEngine` |
| TCP I/O | `IoContext`, `TcpConnection`, `TcpListener` |
| Tox | `ToxAdapter`, `ToxConnection`, `ToxThread` |
| Tunnel | `Tunnel`, `TunnelManager`, `ProtocolFrame` |

### Threading Model

- **I/O thread pool**: Async TCP operations via asio
- **Dedicated Tox thread**: Single thread for all toxcore API calls (toxcore is not thread-safe)
- **Main thread**: Signal handling and orchestration

### Protocol

Binary framing over Tox lossless custom packets:
- Header: `[type:1][tunnel_id:2][length:2]`
- Frame types: `TUNNEL_OPEN`, `TUNNEL_DATA`, `TUNNEL_CLOSE`, `TUNNEL_ACK`, `TUNNEL_ERROR`, `PING`, `PONG`

## Project Structure

```
include/toxtunnel/   # Headers organized by layer (core/, tox/, tunnel/, app/, util/)
src/                 # Implementations mirroring include/ structure
cli/main.cpp         # CLI entry point
tests/unit/          # Unit tests (209 tests)
tests/integration/   # Integration tests (28 tests)
third_party/c-toxcore/  # Git submodule
```

## Dependencies

- **c-toxcore**: Git submodule, built from source
- **asio, spdlog, CLI11, yaml-cpp**: Fetched via CMake FetchContent
- **libsodium**: System package (required by toxcore)
- **Google Test**: Fetched via FetchContent (test builds only)
