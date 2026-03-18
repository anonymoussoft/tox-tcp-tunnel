# TCP Tunnel over Tox - C++ Rewrite Design

**Date:** 2026-03-18
**Project:** Complete C++ rewrite of tuntox
**Goal:** Modern, high-performance TCP tunnel over Tox protocol

## Executive Summary

This document specifies a complete rewrite of tuntox in modern C++, implementing a TCP tunnel over the Tox protocol. The rewrite prioritizes performance, scalability, maintainability, and cross-platform support while providing enhanced features beyond the original C implementation.

**Key Design Decisions:**
- C++20 with modern idioms (RAII, smart pointers, move semantics)
- Hybrid async I/O (asio proactor pattern) + thread pool architecture
- Cross-platform support: Linux, macOS, Windows
- Hybrid error handling: exceptions for fatal errors, error codes for recoverable
- CMake build system with manual dependency management
- Integration with toxcore C library via C++ wrappers

## 1. High-Level Architecture

### 1.1 System Overview

The system follows a layered architecture with clear separation of concerns:

```
┌─────────────────────────────────────────┐
│  CLI / Configuration Layer              │
│  - Argument parsing (CLI11/cxxopts)     │
│  - Config file loading (YAML)           │
└─────────────────────────────────────────┘
           ↓
┌─────────────────────────────────────────┐
│  Application Layer                      │
│  - TunnelServer (accept connections)    │
│  - TunnelClient (initiate tunnels)      │
│  - Connection Manager                   │
└─────────────────────────────────────────┘
           ↓
┌──────────────────┬──────────────────────┐
│  TCP I/O Layer   │  Tox Protocol Layer  │
│  (Asio-based)    │  (Dedicated Thread)  │
│  - IoContext     │  - ToxThread         │
│  - TcpConnection │  - ToxConnection     │
│  - TcpListener   │  - ToxAdapter        │
└──────────────────┴──────────────────────┘
           ↓                ↓
┌──────────────────┬──────────────────────┐
│  Socket Adapter  │  Tox Adapter         │
│  (epoll/kqueue/  │  (toxcore C wrapper) │
│   IOCP)          │                      │
└──────────────────┴──────────────────────┘
```

### 1.2 Design Principles

1. **Separation of concerns**: TCP handling, Tox protocol, and tunnel logic are independent modules
2. **Message passing**: Layers communicate via thread-safe queues (no shared mutable state)
3. **Dependency inversion**: Upper layers depend on interfaces, not concrete implementations
4. **Modern C++**: Leverage C++20 features for safety and expressiveness
5. **YAGNI**: Implement only what's needed, avoid over-engineering

### 1.3 Operating Modes

1. **Server mode**: Accept Tox friend connections, tunnel to local/remote TCP services
2. **Client mode - Port forward**: Listen on local port, forward through Tox
3. **Client mode - Pipe**: Use stdin/stdout (SSH ProxyCommand mode)
4. **Client mode - SOCKS5**: Dynamic port forwarding via SOCKS5 proxy (Phase 2 - see Section 17)

## 2. Threading and Concurrency Model

### 2.1 Thread Pool Architecture

**I/O Thread Pool** (asio::io_context):
- Default: 4 threads (configurable)
- Handles all TCP socket operations
- Executes async callbacks
- Uses proactor pattern (completion-based)

**Tox Thread** (dedicated single thread):
- Runs tox_iterate() event loop
- All toxcore API calls happen here (library is not thread-safe)
- Receives commands via lock-free queue
- Sends events back via another lock-free queue

**Worker Thread Pool** (optional):
- CPU-intensive operations (compression, statistics)
- Separate from I/O threads to avoid blocking

### 2.2 Synchronization Strategy

**Inter-thread communication:**
- Lock-free queues (boost::lockfree::queue or custom implementation)
- Message passing only - no shared mutable state
- Command pattern for cross-thread requests

**Per-connection serialization:**
- Asio strands ensure callbacks for same connection don't run concurrently
- Each Tunnel object has associated strand

**Memory barriers:**
- Atomic operations for reference counting
- Memory ordering guarantees via std::atomic

## 3. Core Components

### 3.1 I/O Core (TCP Layer)

#### IoContext

Wrapper around asio::io_context:

```cpp
class IoContext {
public:
    explicit IoContext(size_t num_threads = 4);
    ~IoContext();

    void run();
    void stop();

    asio::io_context& get_io_context();
    asio::strand<asio::io_context::executor_type> make_strand();

    // Timer services
    template<typename Handler>
    void schedule_after(std::chrono::milliseconds delay, Handler handler);

private:
    asio::io_context io_context_;
    std::vector<std::thread> threads_;
    std::optional<asio::io_context::work> work_guard_;
};
```

#### TcpConnection

RAII wrapper for TCP sockets with async operations:

```cpp
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    enum class State { Connecting, Connected, Closing, Closed };

    static std::shared_ptr<TcpConnection> create(asio::io_context& io);

    // Async operations
    void async_connect(const asio::ip::tcp::endpoint& endpoint,
                      ConnectHandler handler);
    void async_read(std::span<uint8_t> buffer, ReadHandler handler);
    void async_write(std::span<const uint8_t> data, WriteHandler handler);
    void async_close(CloseHandler handler);

    // State management
    State state() const;
    bool is_open() const;

    // Backpressure
    void pause_reading();
    void resume_reading();

    // Socket info
    asio::ip::tcp::endpoint local_endpoint() const;
    asio::ip::tcp::endpoint remote_endpoint() const;

private:
    asio::ip::tcp::socket socket_;
    std::atomic<State> state_;
    bool reading_paused_;
};
```

#### TcpListener

Accepts incoming TCP connections:

```cpp
class TcpListener {
public:
    using AcceptHandler = std::function<void(std::shared_ptr<TcpConnection>)>;

    TcpListener(asio::io_context& io, uint16_t port);

    void start_accept(AcceptHandler handler);
    void stop();

    uint16_t port() const;
    size_t connection_count() const;
    void set_max_connections(size_t max);

private:
    asio::ip::tcp::acceptor acceptor_;
    AcceptHandler accept_handler_;
    size_t connection_count_;
    size_t max_connections_;
};
```

### 3.2 Tox Layer

#### ToxThread

Dedicated thread managing toxcore:

```cpp
// Command types for cross-thread communication
struct Command {
    enum class Type {
        GetToxId,
        AddFriend,
        SendData,
        Shutdown
    };

    Type type;
    std::variant<
        std::monostate,                              // GetToxId
        std::pair<ToxId, std::string>,              // AddFriend (id, message)
        std::pair<uint32_t, std::vector<uint8_t>>   // SendData (friend_num, data)
    > payload;
    std::promise<std::variant<void, ToxId, std::error_code>> result;
};

// Lock-free queue for commands (based on boost::lockfree::spsc_queue)
template<typename T>
using LockFreeQueue = boost::lockfree::spsc_queue<T, boost::lockfree::capacity<1024>>;

// Event queue (thread-safe, uses mutex for simplicity)
class EventQueue {
public:
    struct Event {
        enum class Type {
            FriendRequest,
            FriendConnected,
            FriendDisconnected,
            DataReceived
        };

        Type type;
        uint32_t friend_number;
        std::vector<uint8_t> data;  // For DataReceived
        ToxId tox_id;               // For FriendRequest
    };

    void push(Event event);
    std::optional<Event> try_pop();
    size_t size() const;

private:
    mutable std::mutex mutex_;
    std::queue<Event> queue_;
};

// Custom deleter for Tox pointer
struct ToxDeleter {
    void operator()(Tox* tox) const {
        if (tox) {
            tox_kill(tox);
        }
    }
};

class ToxThread {
public:
    struct Config {
        std::filesystem::path data_dir;
        bool udp_enabled = true;
        uint16_t tcp_relay_port = 0;
        std::vector<BootstrapNode> bootstrap_nodes;
    };

    explicit ToxThread(const Config& config);
    ~ToxThread();

    void start();
    void stop();

    // Commands (thread-safe)
    std::future<ToxId> get_tox_id();
    std::future<void> add_friend(const ToxId& id, std::string_view message);
    std::future<void> send_data(uint32_t friend_number, std::span<const uint8_t> data);

    // Event callbacks (called from Tox thread)
    void set_friend_request_handler(FriendRequestHandler handler);
    void set_friend_connection_handler(FriendConnectionHandler handler);
    void set_friend_message_handler(FriendMessageHandler handler);

private:
    void run_loop();
    void process_commands();
    void handle_tox_events();

    std::unique_ptr<Tox, ToxDeleter> tox_;
    std::thread thread_;
    std::atomic<bool> running_;
    LockFreeQueue<Command> command_queue_;
    EventQueue event_queue_;
};
```

#### ToxConnection

Represents a friend connection:

```cpp
class ToxConnection {
public:
    enum class State { None, Requesting, Connected, Disconnected };

    ToxConnection(uint32_t friend_number);

    uint32_t friend_number() const { return friend_number_; }
    State state() const { return state_; }

    // Send data with flow control
    bool can_send() const;
    size_t send_buffer_space() const;
    void queue_data(std::span<const uint8_t> data);

    // Receive data
    void on_data_received(std::span<const uint8_t> data);

private:
    uint32_t friend_number_;
    std::atomic<State> state_;
    CircularBuffer send_buffer_;
    size_t send_window_;
};
```

#### ToxAdapter

Interface between application and ToxThread:

```cpp
class ToxAdapter {
public:
    explicit ToxAdapter(std::shared_ptr<ToxThread> tox_thread);

    // Connection management
    void connect_to_friend(const ToxId& id, std::string_view secret,
                          ConnectionCallback callback);
    void disconnect_friend(uint32_t friend_number);

    // Data transfer
    void send_frame(uint32_t friend_number, const ProtocolFrame& frame,
                   SendCallback callback);

    // Event subscription
    void on_friend_connected(FriendConnectedHandler handler);
    void on_frame_received(FrameReceivedHandler handler);
    void on_friend_disconnected(FriendDisconnectedHandler handler);

private:
    std::shared_ptr<ToxThread> tox_thread_;
    std::unordered_map<uint32_t, ToxConnection> connections_;
};
```

## 4. Tunnel Protocol

### 4.1 Frame Format

Binary protocol for multiplexing tunnels over Tox:

```
┌─────────────┬──────────────┬─────────────┬──────────────┐
│ Frame Type  │  Tunnel ID   │  Data Length│   Payload    │
│  (1 byte)   │  (2 bytes)   │  (2 bytes)  │  (0-N bytes) │
└─────────────┴──────────────┴─────────────┴──────────────┘

All multi-byte integers in network byte order (big-endian)
Maximum payload: 65535 bytes (limited by length field)
Minimum frame size: 5 bytes (header only, for frames without payload)
```

**Protocol limits:**
- Maximum tunnel ID: 65535 (2-byte field)
- Maximum frame size: 65540 bytes (5-byte header + 65535 payload)
- Maximum tunnels per system: 65535 (tunnel ID space)
- Maximum hostname in TUNNEL_OPEN: 255 bytes (1-byte length field)
- Recommended maximum concurrent tunnels per friend: 100 (configurable)

### 4.2 Frame Types

```cpp
enum class FrameType : uint8_t {
    TUNNEL_OPEN = 0x01,    // Request new tunnel
    TUNNEL_DATA = 0x02,    // Carry TCP data
    TUNNEL_CLOSE = 0x03,   // Graceful close
    TUNNEL_ACK = 0x04,     // Flow control acknowledgment
    TUNNEL_ERROR = 0x05,   // Error notification
    PING = 0x10,           // Keepalive request
    PONG = 0x11            // Keepalive response
};

// Unified protocol frame class for serialization/deserialization
class ProtocolFrame {
public:
    ProtocolFrame(FrameType type, uint16_t tunnel_id);

    // Factory methods for specific frame types
    static ProtocolFrame make_tunnel_open(uint16_t tunnel_id,
                                         const std::string& host,
                                         uint16_t port);
    static ProtocolFrame make_tunnel_data(uint16_t tunnel_id,
                                         std::span<const uint8_t> data);
    static ProtocolFrame make_tunnel_close(uint16_t tunnel_id);
    static ProtocolFrame make_tunnel_ack(uint16_t tunnel_id, uint32_t bytes_acked);
    static ProtocolFrame make_tunnel_error(uint16_t tunnel_id,
                                          uint8_t error_code,
                                          const std::string& description);
    static ProtocolFrame make_ping();
    static ProtocolFrame make_pong();

    // Serialization
    std::vector<uint8_t> serialize() const;

    // Deserialization (returns error if malformed)
    static std::expected<ProtocolFrame, std::error_code>
        deserialize(std::span<const uint8_t> data);

    // Accessors
    FrameType type() const { return type_; }
    uint16_t tunnel_id() const { return tunnel_id_; }
    std::span<const uint8_t> payload() const { return payload_; }

    // Type-specific payload extraction
    struct TunnelOpenPayload {
        std::string host;
        uint16_t port;
    };
    std::optional<TunnelOpenPayload> as_tunnel_open() const;

    std::span<const uint8_t> as_tunnel_data() const;

    struct TunnelAckPayload {
        uint32_t bytes_acked;
    };
    std::optional<TunnelAckPayload> as_tunnel_ack() const;

    struct TunnelErrorPayload {
        uint8_t error_code;
        std::string description;
    };
    std::optional<TunnelErrorPayload> as_tunnel_error() const;

private:
    FrameType type_;
    uint16_t tunnel_id_;
    std::vector<uint8_t> payload_;
};
```

**TUNNEL_OPEN payload:**
```
┌─────────────┬──────────────┬─────────────┐
│  Host Len   │   Hostname   │    Port     │
│  (1 byte)   │  (variable)  │  (2 bytes)  │
└─────────────┴──────────────┴─────────────┘
```

**TUNNEL_DATA payload:**
```
Raw TCP data (0-65535 bytes)
```

**TUNNEL_ACK payload:**
```
┌─────────────┐
│ Bytes Acked │
│  (4 bytes)  │
└─────────────┘
```

**TUNNEL_ERROR payload:**
```
┌─────────────┬──────────────┐
│ Error Code  │  Description │
│  (1 byte)   │  (variable)  │
└─────────────┴──────────────┘
```

### 4.3 Flow Control

**Sliding window protocol:**
- Each tunnel maintains send/receive windows
- Receiver sends TUNNEL_ACK periodically
- Sender pauses when window full
- Window size: 256 KB (configurable)

**Backpressure propagation:**
- Tox send queue full → pause TCP reads
- TCP receive buffer full → pause Tox reads
- Prevents unbounded memory growth

### 4.4 Protocol State Machine

```
Client:                          Server:
  │                                │
  ├─ TUNNEL_OPEN ────────────────>│
  │                                ├─ Validate target
  │                                ├─ Connect to target
  │<──────────── TUNNEL_ACK or ────┤
  │              TUNNEL_ERROR      │
  ├─ TUNNEL_DATA ────────────────>│─ Forward to TCP
  │<───────────── TUNNEL_DATA ─────┤─ Receive from TCP
  ├─ TUNNEL_ACK ─────────────────>│
  │<───────────── TUNNEL_ACK ──────┤
  ├─ TUNNEL_CLOSE ───────────────>│
  │<──────────── TUNNEL_CLOSE ─────┤
  │                                │
```

## 5. Tunnel Management

### 5.0 Supporting Data Structures

#### CircularBuffer

Thread-safe circular buffer for efficient data buffering:

```cpp
class CircularBuffer {
public:
    explicit CircularBuffer(size_t capacity);

    // Write data to buffer (returns bytes written, may be less than requested)
    size_t write(std::span<const uint8_t> data);

    // Read data from buffer (returns bytes read)
    size_t read(std::span<uint8_t> buffer);

    // Peek without consuming
    std::span<const uint8_t> peek(size_t max_bytes) const;

    // Skip bytes without reading
    void consume(size_t bytes);

    // State queries
    size_t size() const;           // Bytes currently in buffer
    size_t capacity() const;       // Total capacity
    size_t available() const;      // Space available for writing
    bool empty() const;
    bool full() const;

    // Clear all data
    void clear();

private:
    std::vector<uint8_t> buffer_;
    size_t read_pos_;
    size_t write_pos_;
    size_t size_;
    mutable std::mutex mutex_;
};
```

### 5.1 Tunnel Object

Represents one bidirectional TCP tunnel:

```cpp
class Tunnel {
public:
    enum class State { Opening, Established, Closing, Closed };

    Tunnel(uint16_t id,
           std::shared_ptr<TcpConnection> tcp_conn,
           uint32_t tox_friend_number);

    uint16_t id() const { return id_; }
    State state() const { return state_; }

    // Data flow
    void on_tcp_data_received(std::span<const uint8_t> data);
    void on_tox_data_received(std::span<const uint8_t> data);

    // Flow control
    void on_ack_received(uint32_t bytes_acked);
    bool can_send_tcp() const;
    bool can_send_tox() const;

    // Lifecycle
    void close();

    // Statistics
    struct Stats {
        uint64_t bytes_sent;
        uint64_t bytes_received;
        std::chrono::steady_clock::time_point created_at;
        std::optional<std::chrono::steady_clock::time_point> closed_at;
    };
    Stats get_stats() const;

private:
    uint16_t id_;
    std::atomic<State> state_;
    std::shared_ptr<TcpConnection> tcp_connection_;
    uint32_t tox_friend_number_;

    // Flow control
    CircularBuffer tcp_to_tox_buffer_;
    CircularBuffer tox_to_tcp_buffer_;
    uint32_t send_window_size_;
    uint32_t send_window_used_;

    // Statistics
    Stats stats_;
};
```

### 5.2 Tunnel Manager

Orchestrates all tunnels:

```cpp
class TunnelManager {
public:
    struct Config {
        size_t max_tunnels_per_friend = 100;
        size_t max_total_tunnels = 1000;
        size_t max_tunnel_buffer_size = 256 * 1024; // 256 KB
        std::chrono::seconds idle_timeout{300}; // 5 minutes
    };

    explicit TunnelManager(const Config& config,
                          std::shared_ptr<ToxAdapter> tox_adapter);

    // Tunnel lifecycle (returns tunnel ID or error)
    std::expected<uint16_t, TunnelError>
        create_tunnel(uint32_t friend_number,
                     std::shared_ptr<TcpConnection> tcp_conn,
                     const std::string& target_host,
                     uint16_t target_port);

    void close_tunnel(uint16_t tunnel_id);
    void close_all_tunnels(uint32_t friend_number);

    // Frame routing
    void route_tcp_to_tox(uint16_t tunnel_id, std::span<const uint8_t> data);
    void route_tox_to_tcp(uint16_t tunnel_id, std::span<const uint8_t> data);

    // Monitoring
    size_t tunnel_count() const;
    size_t tunnel_count_for_friend(uint32_t friend_number) const;
    std::vector<Tunnel::Stats> get_all_stats() const;

private:
    void cleanup_idle_tunnels();
    void enforce_limits(uint32_t friend_number);

    Config config_;
    std::shared_ptr<ToxAdapter> tox_adapter_;

    std::unordered_map<uint16_t, std::unique_ptr<Tunnel>> tunnels_;
    std::unordered_multimap<uint32_t, uint16_t> friend_to_tunnels_;

    std::atomic<uint16_t> next_tunnel_id_{1};
};
```

## 6. Application Logic

### 6.1 TunnelServer

Server mode implementation:

```cpp
class TunnelServer {
public:
    struct Config {
        std::filesystem::path data_dir;
        std::optional<std::string> shared_secret;
        std::optional<std::filesystem::path> rules_file;
        std::vector<ToxId> whitelist;
        TunnelManager::Config tunnel_config;
        bool daemonize = false;
        std::optional<std::filesystem::path> pid_file;
    };

    explicit TunnelServer(const Config& config,
                         std::shared_ptr<IoContext> io_context);

    void start();
    void stop();

    ToxId get_tox_id() const;

private:
    void handle_friend_request(const ToxId& id, std::string_view message);
    void handle_friend_connected(uint32_t friend_number);
    void handle_tunnel_open(uint32_t friend_number, const TunnelOpenFrame& frame);

    bool validate_friend_request(const ToxId& id, std::string_view secret);
    bool validate_target(const std::string& host, uint16_t port);

    Config config_;
    std::shared_ptr<IoContext> io_context_;
    std::shared_ptr<ToxThread> tox_thread_;
    std::shared_ptr<ToxAdapter> tox_adapter_;
    std::unique_ptr<TunnelManager> tunnel_manager_;
    std::unique_ptr<RulesEngine> rules_engine_;
};
```

### 6.2 TunnelClient

Client mode implementation:

```cpp
class TunnelClient {
public:
    struct PortForward {
        uint16_t local_port;
        std::string remote_host;
        uint16_t remote_port;
    };

    enum class Mode { PortForward, Pipe };  // SOCKS5 mode in Phase 2

    struct Config {
        Mode mode;
        ToxId server_id;
        std::optional<std::string> shared_secret;
        std::filesystem::path data_dir;

        // Mode-specific
        std::vector<PortForward> port_forwards; // For PortForward mode
        std::string pipe_target;                 // For Pipe mode (host:port)

        // Connection settings
        bool auto_reconnect = true;
        std::chrono::milliseconds reconnect_delay{1000};
    };

    explicit TunnelClient(const Config& config,
                         std::shared_ptr<IoContext> io_context);

    void start();
    void stop();

private:
    void connect_to_server();
    void handle_server_connected();
    void handle_server_disconnected();

    // Mode implementations
    void run_port_forward_mode();
    void run_pipe_mode();

    void handle_local_connection(std::shared_ptr<TcpConnection> conn,
                                const PortForward& forward);

    Config config_;
    std::shared_ptr<IoContext> io_context_;
    std::shared_ptr<ToxThread> tox_thread_;
    std::shared_ptr<ToxAdapter> tox_adapter_;
    std::unique_ptr<TunnelManager> tunnel_manager_;

    std::vector<std::unique_ptr<TcpListener>> listeners_;
    std::optional<uint32_t> server_friend_number_;
};
```

**Note**: SOCKS5 mode will be added in Phase 2 (see Section 17.1).

### 6.3 Rules Engine

Server-side access control:

```cpp
class RulesEngine {
public:
    enum class Policy { AllowAll, DenyAll };
    enum class Action { Allow, Deny };

    struct Rule {
        std::string host_pattern;  // Glob-style wildcards: * (any chars), ? (single char)
        uint16_t port_min;
        uint16_t port_max;
        Action action;
    };

    explicit RulesEngine(Policy default_policy);

    void load_from_file(const std::filesystem::path& path);
    void add_rule(const Rule& rule);
    void clear_rules();

    bool is_allowed(const std::string& host, uint16_t port) const;

private:
    // Glob-style pattern matching:
    // - '*' matches zero or more characters
    // - '?' matches exactly one character
    // - All other characters match literally (case-sensitive)
    // Examples:
    //   "192.168.*.*" matches "192.168.1.1", "192.168.0.255", etc.
    //   "192.168.1.?" matches "192.168.1.1" through "192.168.1.9"
    //   "localhost" matches only "localhost" exactly
    bool matches_pattern(const std::string& host,
                        const std::string& pattern) const;

    Policy default_policy_;
    std::vector<Rule> rules_;
};
```

**Rules file format:**

```
# Comments start with #
# Format: allow|deny host_pattern[:port_min[-port_max]]
# If port omitted, matches all ports
# If port_max omitted, matches single port

# Allow SSH on localhost
allow localhost:22

# Allow HTTP/HTTPS on LAN (glob-style wildcards)
allow 192.168.*.*:80
allow 192.168.*.*:443

# Allow port range
allow 10.0.0.5:8000-9000

# Allow all ports on specific host
allow 10.0.0.5

# Deny everything else (implicit if default_policy = DenyAll)
```

**Pattern matching rules:**
- `*` matches zero or more characters (greedy)
- `?` matches exactly one character
- Patterns are case-sensitive
- No regex support (keep it simple)
- Port ranges are inclusive: `8000-9000` includes both 8000 and 9000

## 7. Error Handling

### 7.1 Error Categories

**Fatal errors (throw exceptions):**
- Configuration errors (invalid CLI arguments, bad config file)
- Initialization failures (can't bind port, can't load Tox identity)
- Resource exhaustion (out of memory)
- Programming errors (assertion failures, invariant violations)

**Recoverable errors (std::error_code):**
- Network errors (connection refused, timeout, reset)
- Tox protocol errors (friend offline, send failed)
- Tunnel errors (target unreachable, authentication failed)

### 7.2 Error Code System

```cpp
enum class ToxError {
    FriendNotFound = 1,
    FriendOffline,
    SendFailed,
    InvalidToxId,
    BootstrapFailed
};

enum class TunnelError {
    TargetUnreachable = 1,
    AuthenticationFailed,
    TunnelLimitExceeded,
    InvalidTarget,
    ConnectionRefused
};

enum class NetworkError {
    ConnectionRefused = 1,
    ConnectionReset,
    Timeout,
    HostUnreachable
};

// std::error_code integration
std::error_code make_error_code(ToxError e);
std::error_code make_error_code(TunnelError e);
std::error_code make_error_code(NetworkError e);
```

### 7.3 Error Propagation

**Async operations:**
```cpp
void async_operation(Callback<std::error_code, Result> callback);
```

**Sync operations that can fail:**
```cpp
// Using custom result type (C++20 compatible, similar to C++23 std::expected)
template<typename T, typename E>
class Expected {
public:
    bool has_value() const;
    T& value();
    E& error();
    // ... standard expected interface
};

Expected<ToxId, ToxError> parse_tox_id(std::string_view str);
```

**Exception conversion:**
```cpp
try {
    risky_operation();
} catch (const std::system_error& e) {
    log_error("Operation failed: {}", e.what());
    return make_error_code(NetworkError::ConnectionRefused);
}
```

## 8. Logging

### 8.1 Logging Framework

**Library:** spdlog (fast, modern, header-only option available)

**Configuration:**
```cpp
struct LogConfig {
    enum class Level { Trace, Debug, Info, Warn, Error };

    Level level = Level::Info;
    bool log_to_console = true;
    bool log_to_file = false;
    std::filesystem::path log_file;
    size_t max_file_size_mb = 100;
    size_t rotate_count = 5;
    bool async_logging = true;
};
```

### 8.2 Log Levels

- **TRACE**: Frame-level details, buffer operations
- **DEBUG**: Connection events, tunnel lifecycle, state changes
- **INFO**: Server start/stop, friend connections, statistics
- **WARN**: Recoverable errors, rate limiting, retries
- **ERROR**: Fatal errors before exit

### 8.3 Structured Logging

```cpp
// Context-aware logging
log_debug("Tunnel created",
          "tunnel_id", tunnel_id,
          "friend_number", friend_num,
          "target", target_address);

// Produces:
// [2026-03-18 10:23:45.123] [DEBUG] [TunnelManager] Tunnel created
//     tunnel_id=42 friend_number=1 target=192.168.1.1:22
```

### 8.4 Performance Considerations

- Async logging for hot paths (I/O threads don't block on disk writes)
- Compile-time log level filtering (zero overhead for disabled levels)
- Lazy evaluation of expensive log arguments

## 9. Configuration Management

### 9.1 Configuration Sources

Priority order (highest to lowest):
1. Command-line arguments
2. Environment variables (`TOXTUNNEL_*`)
3. Configuration file (YAML)
4. Compiled defaults

### 9.2 Configuration File Format

```yaml
# ~/.toxtunnel/config.yaml

server:
  data_dir: ~/.toxtunnel/server
  shared_secret: ${TOXTUNNEL_SECRET}  # Environment variable substitution
  max_tunnels_per_friend: 100
  max_total_tunnels: 1000
  rules_file: ~/.toxtunnel/rules.txt
  whitelist:
    - "TOXID1..."
    - "TOXID2..."

client:
  data_dir: ~/.toxtunnel/client
  auto_reconnect: true
  reconnect_delay_ms: 1000

tox:
  bootstrap_nodes: ~/.toxtunnel/nodes.json
  tcp_relay_port: 33445
  udp_enabled: true
  udp_port_range: [33445, 33545]

logging:
  level: info
  console: true
  file: ~/.toxtunnel/tunnel.log
  max_size_mb: 100
  rotate_count: 5
  async: true
```

### 9.3 Rules File Format

```
# ~/.toxtunnel/rules.txt
# Format: allow|deny host[:port[-port]]

# Allow SSH on localhost
allow localhost:22

# Allow HTTP/HTTPS on LAN
allow 192.168.*.*:80
allow 192.168.*.*:443

# Allow all ports on specific host
allow 10.0.0.5:*

# Deny everything else (implicit if default_policy = deny)
```

## 10. Command-Line Interface

### 10.1 CLI Design

**Subcommand structure:**

```bash
toxtunnel [GLOBAL_OPTIONS] COMMAND [COMMAND_OPTIONS]
```

**Global options:**
```
--config PATH         Configuration file path
--log-level LEVEL     Override log level (trace|debug|info|warn|error)
--version             Show version and exit
--help                Show help and exit
```

### 10.2 Commands

#### Server Mode

```bash
toxtunnel server [OPTIONS]

Options:
  --data-dir PATH          Data directory (default: ~/.toxtunnel/server)
  --shared-secret SECRET   Authentication secret
  --rules-file PATH        Rules file path
  --daemon                 Run as daemon
  --pid-file PATH          PID file for daemon
  --whitelist TOXID        Allowed Tox ID (can specify multiple)
```

#### Client Mode - Port Forward

```bash
toxtunnel client [OPTIONS] -i TOXID -L LOCAL:REMOTE:PORT

Options:
  -i, --server-id TOXID      Server Tox ID (required)
  -L, --forward SPEC         Port forward spec (can specify multiple)
  --shared-secret SECRET     Authentication secret
  --data-dir PATH            Data directory
  --no-auto-reconnect        Disable auto-reconnect
```

Examples:
```bash
# Forward local 2222 to remote localhost:22
toxtunnel client -i ABC123... -L 2222:localhost:22

# Multiple forwards
toxtunnel client -i ABC123... \
  -L 2222:localhost:22 \
  -L 8080:localhost:80
```

#### Client Mode - Pipe (SSH ProxyCommand)

```bash
toxtunnel client -i TOXID -W REMOTE:PORT

Options:
  -i, --server-id TOXID      Server Tox ID (required)
  -W, --pipe TARGET          Pipe mode target (host:port)
  --shared-secret SECRET     Authentication secret
```

Example:
```bash
# In SSH config:
Host myserver
    ProxyCommand toxtunnel client -i ABC123... -W %h:%p
```

**Note**: SOCKS5 mode (`-D` flag) will be added in Phase 2 (see Section 17.1).

#### Utility Commands

```bash
# Generate new Tox ID
toxtunnel generate-id [--data-dir PATH]

# Show current Tox ID
toxtunnel show-id [--data-dir PATH]

# Ping test
toxtunnel ping TOXID [--timeout SECONDS]

# Version info
toxtunnel version
```

## 11. Performance and Scalability

### 11.1 Performance Targets

**Throughput:**
- Single tunnel: Limited by Tox protocol (~1-2 MB/s typical)
- Multiple tunnels: Aggregate bandwidth scales with CPU cores
- Overhead: <5% CPU for moderate load

**Latency:**
- Additional latency over raw Tox: <10ms (protocol overhead)
- No blocking operations on I/O threads

**Scalability:**
- Server: 100+ concurrent clients, 1000+ active tunnels
- Client: 10+ concurrent tunnels per server connection

### 11.2 Memory Management

**Buffer strategy:**
- Pre-allocated buffer pools (avoid malloc in hot path)
- Circular buffers for per-tunnel data
- Zero-copy operations where possible (scatter-gather I/O)
- Configurable limits: per-tunnel, per-friend, global

**Object lifecycle:**
- Smart pointers for async operation safety (shared_ptr)
- Object pooling for high-churn objects (Tunnel, frames)
- Automatic cleanup via RAII

**Memory limits:**
- Per-tunnel buffer: 256 KB (configurable)
- Global memory cap with backpressure propagation
- Metrics tracking: current, peak, per-component

### 11.3 Optimization Strategies

**I/O optimizations:**
- Batch processing: Coalesce small writes
- Vectored I/O: Use scatter-gather for efficiency
- Direct buffer manipulation: Minimize copies

**Lock-free data structures:**
- Command queues between threads
- Event queues for cross-thread notifications
- Reference counting for shared objects

**CPU cache optimization:**
- Hot path data locality
- Align critical structures to cache lines
- Minimize false sharing

## 12. Security

### 12.1 Authentication

**Shared secret:**
- PBKDF2 or Argon2 key derivation (future enhancement)
- SHA-256 hash comparison (constant-time)
- Secret sent in encrypted friend request message
- Optional key file support

**Whitelist mode:**
- Pre-approved Tox IDs only
- Configurable via file or CLI
- Runtime reload on SIGHUP (Unix)

### 12.2 Attack Surface Mitigation

**DoS protection:**
- Rate limiting: connections per second per friend
- Resource limits: max tunnels, max memory, max frame size
- Idle timeout: auto-close inactive tunnels
- Connection attempt throttling

**Input validation:**
- Validate all protocol frame fields (bounds checking)
- Reject excessive data before buffering
- Sanitize hostnames and ports
- Maximum frame size enforcement

**Secure coding practices:**
- No raw char* (use std::string, std::string_view)
- RAII for all resources
- Compiler hardening: `-fstack-protector`, `-D_FORTIFY_SOURCE=2`
- Static analysis in CI: clang-tidy, cppcheck
- Runtime sanitizers: ASan, UBSan in debug builds

### 12.3 Data Protection

**In-transit:**
- Tox protocol provides end-to-end encryption (NaCl/libsodium)
- No additional encryption needed at tunnel layer

**At-rest:**
- Tox private key file permissions: warn if not 0600
- Optional key file encryption with passphrase
- No sensitive data in logs (truncate Tox IDs, redact secrets)

## 13. Testing Strategy

### 13.1 Unit Tests

**Framework:** Google Test (gtest/gmock)

**Coverage areas:**
- Protocol frame serialization/deserialization
- Tox ID parsing and validation
- Configuration file parsing
- Rules engine (pattern matching)
- Error code conversions
- Buffer management
- State machine transitions

**Mocking:**
- Mock ToxAdapter for testing without real Tox network
- Mock TcpConnection for protocol testing
- Dependency injection throughout

### 13.2 Integration Tests

**Scenarios:**
1. **Local loopback**: Server and client on same machine
2. **Multi-tunnel stress**: 100+ concurrent tunnels
3. **Error conditions**: Network failures, service down, malformed frames
4. **Cross-platform**: Run test suite on Linux, macOS, Windows
5. **Reconnection**: Simulate network interruptions

**Test harness:**
- Spin up server and client processes
- Inject faults (kill processes, firewall rules)
- Validate data integrity
- Measure performance metrics

### 13.3 Performance Tests

**Benchmarks:**
- Throughput: iperf-style test with data verification
- Latency: Ping-pong round-trip time
- Connection rate: Tunnels opened per second
- Memory usage: Track allocations under load

**Continuous monitoring:**
- CI runs performance tests on each commit
- Track regressions (slowdowns >5% flagged)
- Memory leak detection: Valgrind, ASan

## 14. Build System and Project Structure

### 14.1 Directory Structure

```
toxtunnel/
├── CMakeLists.txt               # Top-level build config
├── README.md
├── LICENSE
├── .gitignore
├── .clang-format
├── .clang-tidy
├── docs/
│   ├── architecture.md
│   ├── protocol.md
│   ├── building.md
│   └── superpowers/
│       └── specs/
│           └── 2026-03-18-tcp-tunnel-over-tox-cpp-design.md
├── include/
│   └── toxtunnel/
│       ├── core/
│       │   ├── io_context.hpp
│       │   ├── tcp_connection.hpp
│       │   └── tcp_listener.hpp
│       ├── tox/
│       │   ├── tox_thread.hpp
│       │   ├── tox_connection.hpp
│       │   └── tox_adapter.hpp
│       ├── tunnel/
│       │   ├── protocol.hpp
│       │   ├── tunnel.hpp
│       │   └── tunnel_manager.hpp
│       ├── app/
│       │   ├── tunnel_server.hpp
│       │   ├── tunnel_client.hpp
│       │   └── rules_engine.hpp
│       └── util/
│           ├── error.hpp
│           ├── logger.hpp
│           ├── config.hpp
│           └── buffer.hpp
├── src/
│   ├── core/
│   │   ├── io_context.cpp
│   │   ├── tcp_connection.cpp
│   │   └── tcp_listener.cpp
│   ├── tox/
│   │   ├── tox_thread.cpp
│   │   ├── tox_connection.cpp
│   │   └── tox_adapter.cpp
│   ├── tunnel/
│   │   ├── protocol.cpp
│   │   ├── tunnel.cpp
│   │   └── tunnel_manager.cpp
│   ├── app/
│   │   ├── tunnel_server.cpp
│   │   ├── tunnel_client.cpp
│   │   └── rules_engine.cpp
│   └── util/
│       ├── error.cpp
│       ├── logger.cpp
│       ├── config.cpp
│       └── buffer.cpp
├── cli/
│   └── main.cpp                  # CLI entry point
├── tests/
│   ├── unit/
│   │   ├── test_protocol.cpp
│   │   ├── test_tunnel.cpp
│   │   ├── test_rules_engine.cpp
│   │   └── test_config.cpp
│   ├── integration/
│   │   ├── test_loopback.cpp
│   │   └── test_multi_tunnel.cpp
│   └── performance/
│       ├── bench_throughput.cpp
│       └── bench_latency.cpp
├── third_party/
│   └── CMakeLists.txt            # Fetch dependencies
└── scripts/
    ├── bootstrap_nodes.json
    └── example_config.yaml
```

### 14.2 CMake Build Configuration

**Minimum version:** CMake 3.20

**Top-level CMakeLists.txt structure:**
```cmake
cmake_minimum_required(VERSION 3.20)
project(toxtunnel VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(TOXTUNNEL_BUILD_TESTS "Build tests" ON)
option(TOXTUNNEL_ENABLE_COMPRESSION "Enable compression" ON)
option(TOXTUNNEL_STATIC_LINK "Static linking" OFF)

# Dependencies
find_package(PkgConfig REQUIRED)
pkg_check_modules(TOXCORE REQUIRED toxcore)

include(FetchContent)
FetchContent_Declare(asio ...)
FetchContent_Declare(spdlog ...)
FetchContent_Declare(CLI11 ...)
FetchContent_Declare(yaml-cpp ...)

# Library target
add_library(toxtunnel_lib STATIC ${SOURCES})
target_include_directories(toxtunnel_lib PUBLIC include)
target_link_libraries(toxtunnel_lib PUBLIC asio spdlog yaml-cpp ${TOXCORE_LIBRARIES})

# Executable target
add_executable(toxtunnel cli/main.cpp)
target_link_libraries(toxtunnel PRIVATE toxtunnel_lib CLI11)

# Tests
if(TOXTUNNEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

### 14.3 Dependencies

**Required:**
- C++20 compiler (GCC 10+, Clang 13+, MSVC 2019+)
- CMake 3.20+
- toxcore (c-toxcore) - via system package manager
- asio (standalone) - header-only, via FetchContent
- spdlog - via FetchContent
- CLI11 or cxxopts - via FetchContent
- yaml-cpp - via FetchContent

**Optional:**
- Google Test - for testing
- zlib or lz4 - for compression
- Doxygen - for documentation

**Dependency management:**
- toxcore: System package (apt, brew, vcpkg)
- Header-only libs: CMake FetchContent
- Git submodules as fallback

### 14.4 Compiler Flags

**Debug build:**
```cmake
-Wall -Wextra -Wpedantic -Werror
-fsanitize=address,undefined
-g -O0
```

**Release build:**
```cmake
-Wall -Wextra -Wpedantic
-O3 -DNDEBUG
-fstack-protector-strong
-D_FORTIFY_SOURCE=2
```

## 15. Platform-Specific Considerations

### 15.1 Linux

- Use epoll via asio (automatic)
- Systemd service files for server mode
- Install to `/usr/local/bin` or `/opt/toxtunnel`
- Config in `/etc/toxtunnel` or `~/.config/toxtunnel`

### 15.2 macOS

- Use kqueue via asio (automatic)
- Launch daemon plist for server mode
- Install to `/usr/local/bin` (Homebrew-compatible)
- Config in `~/Library/Application Support/toxtunnel`

### 15.3 Windows

- Use IOCP via asio (automatic)
- Windows service support for server mode
- MSVC and MinGW support
- Config in `%APPDATA%\toxtunnel`
- Handle DLL exports for shared library build

## 16. Migration Path from Original Tuntox

### 16.1 Configuration Compatibility

**Not compatible:**
- Command-line arguments redesigned (subcommands)
- Config file format changed (YAML instead of C conventions)

**Migration guide:**
- Document CLI argument mappings
- Provide conversion script for common use cases
- Support same default directories for Tox save data

### 16.2 Protocol Compatibility

**On-wire protocol:**
- Start with compatible protocol (reuse frame types)
- Version field in initial handshake for future evolution
- Graceful degradation when possible

**Future enhancements:**
- Optional compression (negotiated)
- Improved flow control (new frame types)
- Multiplexing optimization

## 17. Future Enhancements (Out of Scope for Initial Release)

### 17.1 Phase 2 Features

**SOCKS5 proxy mode:**
- Support SOCKS5 protocol (RFC 1928) for dynamic port forwarding
- Authentication methods: no-auth, username/password
- CONNECT command support (TCP tunneling)
- IPv4 and IPv6 address types
- DNS resolution on server side
- CLI: `toxtunnel client -i TOXID -D 1080` (listen on local port 1080)
- Integration: Extend TunnelClient with Socks5Server component

**Other Phase 2 features:**
- C++20 coroutines (migrate from callbacks for cleaner async code)
- Compression support (zlib, lz4) - negotiated per tunnel
- IPv6 support (if toxcore supports it)
- Built-in monitoring/metrics endpoint (Prometheus-compatible)
- Configuration hot-reload (SIGHUP)

### 17.2 Phase 3 Features

- TLS termination mode (tunnel TLS connections)
- HTTP/HTTPS proxy mode (not just SOCKS5)
- Connection pooling and multiplexing optimizations
- Plugin system for custom protocols
- Web-based management interface

## 18. Success Criteria

### 18.1 Functional Requirements

✓ Server accepts Tox connections and forwards to TCP services
✓ Client can connect via port forward and pipe modes
✓ Multiple concurrent tunnels work correctly
✓ Authentication via shared secret works
✓ Rules engine allows/denies targets correctly
✓ Reconnection on network failure works

### 18.2 Non-Functional Requirements

✓ Performance: Single tunnel achieves >80% of raw Tox throughput
✓ Scalability: Handle 100+ clients and 1000+ tunnels on server
✓ Stability: Run 24/7 without memory leaks or crashes
✓ Cross-platform: Build and run on Linux, macOS, Windows
✓ Code quality: Pass static analysis, >80% test coverage

### 18.3 Usability Requirements

✓ Clear documentation (README, building, usage)
✓ Intuitive CLI with help text
✓ Useful error messages (not just "failed")
✓ Example configurations provided

## 19. Implementation Phases

### 19.1 Phase 1: Foundation (Weeks 1-2)

- Project structure and CMake setup
- Core I/O abstractions (IoContext, TcpConnection)
- Tox wrapper (ToxThread, ToxAdapter)
- Basic protocol frame serialization
- Unit tests for core components

### 19.2 Phase 2: Tunnel Logic (Weeks 3-4)

- Tunnel and TunnelManager implementation
- Protocol state machine
- Flow control and backpressure
- Integration tests for tunneling

### 19.3 Phase 3: Application Layer (Weeks 5-6)

- TunnelServer implementation
- TunnelClient (port forward mode)
- Client pipe mode
- Rules engine
- Configuration loading
- CLI implementation

### 19.4 Phase 4: Polish (Weeks 7-8)

- Error handling refinement
- Performance optimization
- Documentation
- Cross-platform testing
- Packaging (installers, Docker images)

## 20. Conclusion

This design provides a solid foundation for a modern, high-performance TCP tunnel over Tox. The architecture prioritizes:

- **Performance**: Async I/O, zero-copy operations, efficient threading
- **Scalability**: Lock-free queues, connection pooling, resource limits
- **Maintainability**: Clean separation of concerns, modern C++, testability
- **Reliability**: Comprehensive error handling, flow control, reconnection
- **Security**: Authentication, access control, DoS mitigation
- **Cross-platform**: Abstraction over platform differences

The design is ambitious but achievable, building on proven patterns (proactor, message passing) and libraries (asio, toxcore). The phased implementation approach allows for incremental progress and early validation of key architectural decisions.
