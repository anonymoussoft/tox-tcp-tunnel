#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string>
#include <string_view>

namespace toxtunnel::util {

/// Log severity levels, mirroring spdlog levels.
enum class LogLevel {
    Trace = SPDLOG_LEVEL_TRACE,
    Debug = SPDLOG_LEVEL_DEBUG,
    Info = SPDLOG_LEVEL_INFO,
    Warn = SPDLOG_LEVEL_WARN,
    Error = SPDLOG_LEVEL_ERROR,
    Critical = SPDLOG_LEVEL_CRITICAL,
    Off = SPDLOG_LEVEL_OFF,
};

/// A thin facade around spdlog providing console and optional file logging.
///
/// Typical usage:
/// @code
///   Logger::init("toxtunnel");
///   Logger::set_level(LogLevel::Debug);
///   Logger::add_file_sink("/var/log/toxtunnel.log");
///
///   Logger::info("listening on port {}", port);
///   Logger::error("connection failed: {}", ec.message());
/// @endcode
///
/// The class is entirely static; there is no need to instantiate it.
/// Thread safety is guaranteed by spdlog.
class Logger {
   public:
    Logger() = delete;

    // -----------------------------------------------------------------
    // Initialisation & configuration
    // -----------------------------------------------------------------

    /// Initialise the global logger with a console (stderr) sink.
    /// Must be called once before any logging.
    /// @param name  Logger name shown in log output.
    static void init(std::string_view name = "toxtunnel");

    /// Shut down and flush all sinks.  Optional; spdlog also flushes on
    /// process exit.
    static void shutdown();

    /// Set the minimum log level.  Messages below this level are discarded.
    static void set_level(LogLevel level);

    /// Return the current minimum log level.
    static LogLevel get_level();

    /// Set the log pattern (spdlog pattern syntax).
    /// @see https://github.com/gabime/spdlog/wiki/3.-Custom-formatting
    static void set_pattern(std::string_view pattern);

    /// Add a rotating-file sink.
    /// @param filename       Path to the log file.
    /// @param max_size_bytes Maximum size of a single file before rotation
    ///                       (default 5 MiB).
    /// @param max_files      Number of rotated files to keep (default 3).
    static void add_file_sink(const std::string& filename,
                              std::size_t max_size_bytes = 5 * 1024 * 1024,
                              std::size_t max_files = 3);

    /// Immediately flush all sinks.
    static void flush();

    // -----------------------------------------------------------------
    // Logging methods
    // -----------------------------------------------------------------

    template <typename... Args>
    static void trace(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        get()->trace(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void debug(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        get()->debug(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void info(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        get()->info(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void warn(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        get()->warn(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void error(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        get()->error(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void critical(spdlog::format_string_t<Args...> fmt, Args&&... args) {
        get()->critical(fmt, std::forward<Args>(args)...);
    }

    // -----------------------------------------------------------------
    // Raw access
    // -----------------------------------------------------------------

    /// Return the underlying spdlog logger.
    /// Useful when a library or subsystem expects an `std::shared_ptr<spdlog::logger>`.
    static std::shared_ptr<spdlog::logger> get();

   private:
    /// Convert our LogLevel enum to the matching spdlog enum value.
    static spdlog::level::level_enum to_spdlog_level(LogLevel level);

    /// Convert spdlog enum value back to our LogLevel.
    static LogLevel from_spdlog_level(spdlog::level::level_enum level);
};

}  // namespace toxtunnel::util
