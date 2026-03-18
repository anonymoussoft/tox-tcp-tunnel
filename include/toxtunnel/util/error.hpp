#pragma once

#include <string>
#include <system_error>

namespace toxtunnel {

// ---------------------------------------------------------------------------
// Error enums
// ---------------------------------------------------------------------------

/// Errors related to the Tox protocol and toxcore operations.
enum class ToxError {
    FriendNotFound = 1,
    FriendOffline,
    SendFailed,
    InvalidToxId,
    BootstrapFailed,
};

/// Errors related to the tunnel protocol layer.
enum class TunnelError {
    TargetUnreachable = 1,
    AuthenticationFailed,
    TunnelLimitExceeded,
    InvalidTarget,
    ConnectionRefused,
};

/// Errors related to network / TCP operations.
enum class NetworkError {
    ConnectionRefused = 1,
    ConnectionReset,
    Timeout,
    HostUnreachable,
};

// ---------------------------------------------------------------------------
// Error categories
// ---------------------------------------------------------------------------

/// Returns the singleton error_category for ToxError codes.
const std::error_category& tox_error_category() noexcept;

/// Returns the singleton error_category for TunnelError codes.
const std::error_category& tunnel_error_category() noexcept;

/// Returns the singleton error_category for NetworkError codes.
const std::error_category& network_error_category() noexcept;

// ---------------------------------------------------------------------------
// make_error_code overloads (found via ADL)
// ---------------------------------------------------------------------------

std::error_code make_error_code(ToxError e) noexcept;
std::error_code make_error_code(TunnelError e) noexcept;
std::error_code make_error_code(NetworkError e) noexcept;

}  // namespace toxtunnel

// ---------------------------------------------------------------------------
// Enable implicit conversion to std::error_code
// ---------------------------------------------------------------------------

template <>
struct std::is_error_code_enum<toxtunnel::ToxError> : std::true_type {};

template <>
struct std::is_error_code_enum<toxtunnel::TunnelError> : std::true_type {};

template <>
struct std::is_error_code_enum<toxtunnel::NetworkError> : std::true_type {};
