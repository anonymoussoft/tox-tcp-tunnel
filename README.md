# ToxTunnel

TCP tunneling over the [Tox](https://tox.chat) protocol. A modern C++ implementation of a TCP tunnel over Tox.

ToxTunnel lets you forward TCP ports through the Tox peer-to-peer network. Traffic is end-to-end encrypted by Tox, requires no centralized server, and traverses NAT automatically. Run a **server** on the machine you want to reach and a **client** on the machine you're connecting from.

## Features

- End-to-end encrypted TCP tunneling via Tox
- Server and client modes with YAML configuration
- Port forwarding (map local ports to remote host:port)
- Per-friend access control rules (allow/deny by host pattern, IP, port)
- Async I/O with thread pool (asio)
- Binary framing protocol with flow control and keep-alive
- Cross-platform (Linux, macOS, Windows)

## Building

### Prerequisites

| Dependency | Version | Notes |
|---|---|---|
| C++ compiler | GCC 10+, Clang 13+, or MSVC 2019+ | Must support C++20 |
| CMake | 3.16+ | Build system generator |
| git | any | For cloning and submodules |
| pkg-config | any | Required by c-toxcore's build (not needed on Windows/MSVC) |
| libsodium | any | Cryptography library, required by c-toxcore |

toxcore is included as a git submodule and built from source. Other C++ dependencies (asio, spdlog, CLI11, yaml-cpp, googletest) are fetched automatically by CMake via FetchContent. No additional manual installation is needed for those.

### Installing dependencies by platform

#### macOS (x86_64 / aarch64)

Homebrew installs native packages for both Intel and Apple Silicon automatically.

```bash
# Install Xcode Command Line Tools (provides Clang with C++20 support)
xcode-select --install

# Install dependencies via Homebrew
brew install cmake pkg-config libsodium
```

#### Linux -- Ubuntu / Debian (x86_64 / aarch64)

```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config libsodium-dev
```

`build-essential` provides GCC (with C++20 support on Ubuntu 22.04+ / Debian 12+).

#### Linux -- Fedora / RHEL (x86_64 / aarch64)

```bash
sudo dnf install -y gcc-c++ cmake git pkgconf-pkg-config libsodium-devel
```

#### Linux -- Arch Linux (x86_64)

```bash
sudo pacman -S --needed base-devel cmake git pkgconf libsodium
```

#### Windows (x86_64 / aarch64)

**Option A: MSVC + vcpkg**

1. Install [Visual Studio 2022](https://visualstudio.microsoft.com/) (or Build Tools) with the "Desktop development with C++" workload.
2. Install [CMake](https://cmake.org/download/) (or use the one bundled with Visual Studio).
3. Install [vcpkg](https://github.com/microsoft/vcpkg) and install libsodium:

```powershell
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg install libsodium:x64-windows      # for x86_64
.\vcpkg install libsodium:arm64-windows    # for aarch64
```

When configuring CMake, pass the vcpkg toolchain file:

```powershell
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

**Option B: MSYS2 / MinGW-w64**

1. Install [MSYS2](https://www.msys2.org/).
2. Open the MSYS2 UCRT64 shell and install dependencies:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-pkg-config mingw-w64-ucrt-x86_64-libsodium git
```

Then build using the standard CMake commands below inside the MSYS2 shell.

### Clone and build

```bash
git clone --recursive https://github.com/anonymoussoft/tox-tcp-tunnel.git
cd tox-tcp-tunnel

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

If you already cloned without `--recursive`, fetch submodules first:

```bash
git submodule update --init --recursive
```

For parallel builds, pass `-j` with the number of CPU cores:

```bash
cmake --build build -j$(nproc)        # Linux
cmake --build build -j$(sysctl -n hw.ncpu)  # macOS
```

On Windows with MSVC, use:

```powershell
cmake --build build --config Release --parallel
```

### CMake options

| Option | Default | Description |
|---|---|---|
| `TOXTUNNEL_BUILD_TESTS` | `ON` | Build unit and integration tests |
| `TOXTUNNEL_ENABLE_ASAN` | `OFF` | Enable AddressSanitizer (debug builds) |

### Running tests

```bash
./build/tests/unit_tests
./build/tests/integration_tests

# Or via CTest
cd build && ctest --output-on-failure
```

On Windows with MSVC, test binaries are under the configuration subdirectory:

```powershell
.\build\tests\Release\unit_tests.exe
.\build\tests\Release\integration_tests.exe
```

### Building with Docker

You can build ToxTunnel inside a Docker container without installing dependencies on the host. The examples below use `ubuntu:24.04`, which supports both `linux/amd64` and `linux/arm64` architectures natively.

#### Quick build (single architecture)

```bash
docker run --rm -v "$(pwd)":/src -w /src ubuntu:24.04 bash -c '
    apt-get update &&
    apt-get install -y build-essential cmake git pkg-config libsodium-dev &&
    cmake -B build -DCMAKE_BUILD_TYPE=Release &&
    cmake --build build -j$(nproc) &&
    build/tests/unit_tests &&
    build/tests/integration_tests
'
```

The built binary is at `build/bin/toxtunnel` on the host after the container exits.

#### Using a Dockerfile

Create a `Dockerfile` (or use the one below) for reproducible builds:

```dockerfile
FROM ubuntu:24.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        pkg-config \
        libsodium-dev \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc)

# Run tests as a build verification step
RUN build/tests/unit_tests && build/tests/integration_tests

# Copy binary to a clean location
RUN cp build/bin/toxtunnel /usr/local/bin/toxtunnel

ENTRYPOINT ["toxtunnel"]
```

Build and run:

```bash
docker build -t toxtunnel .
docker run --rm toxtunnel --help
```

#### Multi-architecture build (x86_64 + aarch64)

Use Docker Buildx to build images for both architectures:

```bash
# Create a buildx builder (one-time setup)
docker buildx create --name multiarch --use

# Build and push for both architectures
docker buildx build --platform linux/amd64,linux/arm64 -t toxtunnel:latest .
```

#### Extract the binary from a Docker build

To extract just the compiled binary without keeping the image:

```bash
docker build -t toxtunnel-build .
docker create --name extract toxtunnel-build
docker cp extract:/usr/local/bin/toxtunnel ./toxtunnel
docker rm extract
```

## Usage

```
toxtunnel [OPTIONS]
```

### CLI options

| Flag | Description |
|---|---|
| `-c, --config FILE` | Path to YAML config file |
| `-m, --mode MODE` | Operating mode: `server` or `client` |
| `-d, --data-dir DIR` | Override data directory for Tox save files |
| `-l, --log-level LEVEL` | Log level: `trace`, `debug`, `info`, `warn`, `error` |
| `-p, --port PORT` | Override TCP port (server mode) |
| `--server-id ID` | Override server Tox ID (client mode) |
| `-v, --version` | Show version |

### Quick start

**Server** (on the machine you want to reach):

```bash
toxtunnel -m server
```

The server prints its Tox address on startup. Copy it to use on the client.

**Client** (on the machine you're connecting from):

```bash
toxtunnel -m client --server-id <SERVER_TOX_ADDRESS>
```

With a config file for port forwarding:

```bash
toxtunnel -c config.yaml
```

## Configuration

ToxTunnel uses YAML configuration files. You can also override most settings via CLI flags.

### Server configuration

```yaml
mode: server
data_dir: /var/lib/toxtunnel

logging:
  level: info
  file: /var/log/toxtunnel.log     # optional

server:
  tcp_port: 33445
  udp_enabled: true
  rules_file: /etc/toxtunnel/rules.yaml   # optional access control

  bootstrap_nodes:
    - address: tox.node.example.com
      port: 33445
      public_key: "AABBCCDD..."           # 64 hex characters
```

### Client configuration

```yaml
mode: client
data_dir: ~/.toxtunnel

logging:
  level: info

client:
  server_id: "AABBCCDD..."               # 76 hex characters (full Tox address)

  forwards:
    - local_port: 2222
      remote_host: 127.0.0.1
      remote_port: 22                     # forward local:2222 -> remote:22

    - local_port: 8080
      remote_host: 192.168.1.100
      remote_port: 80
```

With this client config, connecting to `localhost:2222` tunnels traffic through Tox to port 22 on the server side.

### Access control rules

The server can restrict which friends may connect to which destinations. Rules are loaded from a separate YAML file referenced by `server.rules_file`.

```yaml
# rules.yaml
rules:
  - friend_public_key: "AABBCCDD..."     # 64 hex characters
    allow:
      - host: "127.0.0.1"
        ports: [22, 80, 443]
      - host: "*.internal.example.com"
        ports: []                         # empty = all ports
    deny:
      - host: "10.*"
        ports: []
```

Pattern matching:

- `*` matches any sequence of characters (`*.example.com`, `192.168.*`)
- `?` matches a single character
- Host matching is case-insensitive
- IP matching supports per-octet wildcards (`192.168.*.*`)
- Deny rules take precedence over allow rules

## Architecture

```
+-------------------+
|    CLI / Config   |    YAML config, CLI11 argument parsing
+-------------------+
         |
+-------------------+
| Application Layer |    TunnelServer / TunnelClient / RulesEngine
+-------------------+
         |
+--------+----------+
|  TCP   |   Tox    |
|  I/O   | Protocol |    Asio thread pool / dedicated Tox thread
+--------+----------+
```

### Key components

| Component | Description |
|---|---|
| `TunnelServer` | Accepts Tox friend connections, forwards to local TCP services |
| `TunnelClient` | Listens on local TCP ports, tunnels through Tox to the server |
| `TunnelManager` | Manages multiple concurrent tunnels per friend connection |
| `Tunnel` | State machine for a single bidirectional tunnel |
| `ProtocolFrame` | Binary frame serialization/deserialization |
| `ToxAdapter` | High-level wrapper for the toxcore C API |
| `ToxThread` | Dedicated thread for the toxcore event loop |
| `RulesEngine` | Per-friend access control (allow/deny rules) |
| `IoContext` | Async I/O thread pool wrapping asio |
| `Config` | YAML configuration loading, validation, and CLI override merging |

### Protocol

ToxTunnel uses a binary framing protocol over Tox lossless custom packets:

```
Offset  Size  Field
------  ----  -----
0       1     type       (FrameType)
1       2     tunnel_id  (uint16, big-endian)
3       2     length     (uint16, big-endian)
5       N     payload
```

Frame types: `TUNNEL_OPEN` (0x01), `TUNNEL_DATA` (0x02), `TUNNEL_CLOSE` (0x03), `TUNNEL_ACK` (0x04), `TUNNEL_ERROR` (0x05), `PING` (0x10), `PONG` (0x11).

### Threading model

- **I/O thread pool** -- async TCP operations (asio)
- **Tox thread** -- single dedicated thread for all toxcore API calls (toxcore is not thread-safe)
- **Main thread** -- signal handling and orchestration

### Dependencies

| Library | Version | Purpose |
|---|---|---|
| [c-toxcore](https://github.com/TokTok/c-toxcore) | v0.2.22 | Tox protocol (git submodule, built from source) |
| [asio](https://github.com/chriskohlhoff/asio) | 1.28.0 | Async I/O (FetchContent, header-only) |
| [spdlog](https://github.com/gabime/spdlog) | 1.12.0 | Logging (FetchContent) |
| [CLI11](https://github.com/CLIUtils/CLI11) | 2.3.2 | CLI argument parsing (FetchContent) |
| [yaml-cpp](https://github.com/jbeder/yaml-cpp) | 0.7.0 | YAML parsing (FetchContent) |
| libsodium | system | Cryptography (required by toxcore) |
| [Google Test](https://github.com/google/googletest) | latest | Testing (FetchContent, test builds only) |

## Project structure

```
tox-tcp-tunnel/
  cli/
    main.cpp                  # CLI entry point
  include/toxtunnel/
    core/                     # Async I/O primitives
      io_context.hpp
      tcp_connection.hpp
      tcp_listener.hpp
    tox/                      # Tox protocol layer
      types.hpp
      tox_adapter.hpp
      tox_connection.hpp
      tox_thread.hpp
      tox_save.hpp
    tunnel/                   # Tunnel protocol and management
      protocol.hpp
      tunnel.hpp
      tunnel_manager.hpp
    app/                      # Application logic
      tunnel_server.hpp
      tunnel_client.hpp
      rules_engine.hpp
    util/                     # Utilities
      config.hpp
      logger.hpp
      error.hpp
      expected.hpp
      circular_buffer.hpp
  src/                        # Implementation files (mirrors include/)
  tests/
    unit/                     # Unit tests (209 tests)
    integration/              # Integration tests (28 tests)
  third_party/
    c-toxcore/                # toxcore git submodule
  docs/
    superpowers/specs/        # Design specification
    superpowers/plans/        # Implementation plan
```

## License

GPLv3 -- see [LICENSE](LICENSE).
