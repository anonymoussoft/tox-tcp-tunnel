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

- C++20 compiler (GCC 10+, Clang 13+, MSVC 2019+)
- CMake 3.16+
- libsodium (`brew install libsodium` / `apt install libsodium-dev`)

toxcore is included as a git submodule and built from source. No system toxcore installation is needed.

### Clone and build

```bash
git clone --recursive https://github.com/<you>/tox-tcp-tunnel.git
cd tox-tcp-tunnel

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

If you already cloned without `--recursive`, fetch submodules first:

```bash
git submodule update --init --recursive
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
