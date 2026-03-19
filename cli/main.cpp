#include <CLI/CLI.hpp>
#include <asio.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <thread>

#include "toxtunnel/app/tunnel_client.hpp"
#include "toxtunnel/app/tunnel_server.hpp"
#include "toxtunnel/util/config.hpp"
#include "toxtunnel/util/logger.hpp"

namespace {

constexpr const char* kVersion = "1.0.0";

/// Map from CLI string to LogLevel.
const std::map<std::string, toxtunnel::util::LogLevel> kLogLevelMap = {
    {"trace", toxtunnel::util::LogLevel::Trace},
    {"debug", toxtunnel::util::LogLevel::Debug},
    {"info", toxtunnel::util::LogLevel::Info},
    {"warn", toxtunnel::util::LogLevel::Warn},
    {"error", toxtunnel::util::LogLevel::Error},
};

/// Parse a log level string into a LogLevel enum value.
/// Returns true on success, false on failure.
bool parse_log_level(const std::string& str, toxtunnel::util::LogLevel& out) {
    auto it = kLogLevelMap.find(str);
    if (it != kLogLevelMap.end()) {
        out = it->second;
        return true;
    }
    return false;
}

/// Run the tunnel server until a signal is received.
int run_server(const toxtunnel::Config& config) {
    using Logger = toxtunnel::util::Logger;

    toxtunnel::app::TunnelServer server;

    auto init_result = server.initialize(config);
    if (!init_result.has_value()) {
        Logger::error("Failed to initialize server: {}", init_result.error());
        return 1;
    }

    Logger::info("Server initialized successfully");

    if (config.server) {
        Logger::info("Listening on TCP port {}", config.server->tcp_port);
    }

    // Print the Tox address so clients can connect
    auto tox_address = server.get_tox_address();
    if (!tox_address.empty()) {
        Logger::info("Server Tox address: {}", tox_address);
    }

    // Set up signal handling via asio
    asio::io_context signal_ctx;
    asio::signal_set signals(signal_ctx, SIGINT, SIGTERM);

    signals.async_wait([&server](const asio::error_code& ec, int signum) {
        if (!ec) {
            toxtunnel::util::Logger::info("Received signal {}, shutting down...", signum);
            server.stop();
        }
    });

    // Start the server (non-blocking)
    server.start();
    Logger::info("Server started");

    // Block on signal wait
    signal_ctx.run();

    Logger::info("Server stopped");
    return 0;
}

/// Run the tunnel client until a signal is received.
int run_client(const toxtunnel::Config& config) {
    using Logger = toxtunnel::util::Logger;

    toxtunnel::app::TunnelClient client;

    auto init_result = client.initialize(config);
    if (!init_result.has_value()) {
        Logger::error("Failed to initialize client: {}", init_result.error());
        return 1;
    }

    Logger::info("Client initialized successfully");

    if (config.client && !config.client->server_id.empty()) {
        Logger::info("Connecting to server: {}", config.client->server_id);
    }

    // Set up signal handling via asio
    asio::io_context signal_ctx;
    asio::signal_set signals(signal_ctx, SIGINT, SIGTERM);

    signals.async_wait([&client](const asio::error_code& ec, int signum) {
        if (!ec) {
            toxtunnel::util::Logger::info("Received signal {}, shutting down...", signum);
            client.stop();
        }
    });

    // Start the client (non-blocking)
    client.start();
    Logger::info("Client started");

    std::thread signal_thread([&signal_ctx] {
        signal_ctx.run();
    });

    client.wait_until_stopped();
    signal_ctx.stop();
    if (signal_thread.joinable()) {
        signal_thread.join();
    }

    Logger::info("Client stopped");
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    using namespace toxtunnel;
    using Logger = util::Logger;

    // -----------------------------------------------------------------------
    // CLI argument parsing
    // -----------------------------------------------------------------------
    CLI::App app{"ToxTunnel - TCP Tunnel over Tox"};

    std::string config_path;
    std::string mode_str;
    std::string data_dir;
    std::string log_level_str;
    uint16_t port = 0;
    std::string server_id;
    std::string pipe_target;

    app.add_option("-c,--config", config_path, "Path to YAML config file");

    app.add_option("-m,--mode", mode_str, "Operating mode: server or client")
        ->check(CLI::IsMember({"server", "client"}));

    app.add_option("-d,--data-dir", data_dir, "Override data directory");

    app.add_option("-l,--log-level", log_level_str, "Override log level")
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "error"}));

    app.add_option("-p,--port", port, "Override TCP port (server mode)")
        ->check(CLI::Range(static_cast<uint16_t>(1), static_cast<uint16_t>(65535)));

    app.add_option("--server-id", server_id, "Override server Tox ID (client mode)");
    app.add_option("--pipe", pipe_target, "Pipe mode target host:port (client mode)");

    app.set_version_flag("-v,--version", kVersion);

    CLI11_PARSE(app, argc, argv);

    // -----------------------------------------------------------------------
    // Load configuration
    // -----------------------------------------------------------------------
    Config config;

    if (!config_path.empty()) {
        // Explicit config file specified
        auto result = Config::from_file(config_path);
        if (!result.has_value()) {
            // Logger not initialized yet, use std::cerr for bootstrap errors
            std::cerr << "Error loading config: " << result.error() << "\n";
            return 1;
        }
        config = std::move(result).value();
    } else {
        // No config specified; use defaults based on mode
        if (mode_str == "client") {
            config = Config::default_client();
        } else {
            config = Config::default_server();
        }
    }

    // -----------------------------------------------------------------------
    // Apply CLI overrides
    // -----------------------------------------------------------------------
    Config overrides;
    bool has_overrides = false;

    if (!mode_str.empty()) {
        if (mode_str == "server") {
            overrides.mode = Mode::Server;
            overrides.server = ServerConfig{};
        } else {
            overrides.mode = Mode::Client;
            overrides.client = ClientConfig{};
        }
        has_overrides = true;
    } else {
        // Keep same mode as loaded config so merge_cli_overrides does not
        // interpret a default Mode::Server as an intentional override.
        overrides.mode = config.mode;
    }

    if (!data_dir.empty()) {
        overrides.data_dir = data_dir;
        has_overrides = true;
    }

    if (!log_level_str.empty()) {
        util::LogLevel level{};
        if (parse_log_level(log_level_str, level)) {
            overrides.logging.level = level;
            has_overrides = true;
        }
    }

    if (port != 0) {
        if (!overrides.server) {
            overrides.server = ServerConfig{};
        }
        overrides.server->tcp_port = port;
        has_overrides = true;
    }

    if (!server_id.empty()) {
        if (!overrides.client) {
            overrides.client = ClientConfig{};
        }
        overrides.client->server_id = server_id;
        has_overrides = true;
    }

    if (!pipe_target.empty()) {
        auto pipe_result = parse_pipe_target(pipe_target);
        if (!pipe_result) {
            std::cerr << "Configuration error: " << pipe_result.error() << "\n";
            return 1;
        }
        if (!overrides.client) {
            overrides.client = ClientConfig{};
        }
        overrides.client->pipe_target = pipe_result.value();
        has_overrides = true;
    }

    if (has_overrides) {
        config.merge_cli_overrides(overrides);
    }

    // -----------------------------------------------------------------------
    // Validate configuration
    // -----------------------------------------------------------------------
    auto validation = config.validate();
    if (!validation.has_value()) {
        std::cerr << "Configuration error: " << validation.error() << "\n";
        return 1;
    }

    // -----------------------------------------------------------------------
    // Initialize logging
    // -----------------------------------------------------------------------
    Logger::init("toxtunnel");
    Logger::set_level(config.logging.level);

    if (config.logging.file.has_value()) {
        Logger::add_file_sink(*config.logging.file);
    }

    Logger::info("ToxTunnel v{} starting in {} mode",
                 kVersion,
                 config.is_server() ? "server" : "client");
    Logger::debug("Data directory: {}", config.data_dir.string());

    // -----------------------------------------------------------------------
    // Run the appropriate mode
    // -----------------------------------------------------------------------
    int exit_code = 0;

    if (config.is_server()) {
        exit_code = run_server(config);
    } else {
        exit_code = run_client(config);
    }

    Logger::info("ToxTunnel exiting with code {}", exit_code);
    Logger::shutdown();

    return exit_code;
}
