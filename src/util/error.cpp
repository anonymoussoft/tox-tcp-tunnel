#include "toxtunnel/util/error.hpp"

namespace toxtunnel {

// ===========================================================================
// ToxErrorCategory
// ===========================================================================

namespace {

class ToxErrorCategory final : public std::error_category {
   public:
    const char* name() const noexcept override { return "tox"; }

    std::string message(int ev) const override {
        switch (static_cast<ToxError>(ev)) {
            case ToxError::FriendNotFound:
                return "friend not found";
            case ToxError::FriendOffline:
                return "friend is offline";
            case ToxError::SendFailed:
                return "failed to send data via tox";
            case ToxError::InvalidToxId:
                return "invalid tox ID";
            case ToxError::BootstrapFailed:
                return "tox bootstrap failed";
            default:
                return "unknown tox error (" + std::to_string(ev) + ")";
        }
    }
};

// ===========================================================================
// TunnelErrorCategory
// ===========================================================================

class TunnelErrorCategory final : public std::error_category {
   public:
    const char* name() const noexcept override { return "tunnel"; }

    std::string message(int ev) const override {
        switch (static_cast<TunnelError>(ev)) {
            case TunnelError::TargetUnreachable:
                return "tunnel target is unreachable";
            case TunnelError::AuthenticationFailed:
                return "tunnel authentication failed";
            case TunnelError::TunnelLimitExceeded:
                return "tunnel limit exceeded";
            case TunnelError::InvalidTarget:
                return "invalid tunnel target";
            case TunnelError::ConnectionRefused:
                return "tunnel connection refused";
            default:
                return "unknown tunnel error (" + std::to_string(ev) + ")";
        }
    }
};

// ===========================================================================
// NetworkErrorCategory
// ===========================================================================

class NetworkErrorCategory final : public std::error_category {
   public:
    const char* name() const noexcept override { return "network"; }

    std::string message(int ev) const override {
        switch (static_cast<NetworkError>(ev)) {
            case NetworkError::ConnectionRefused:
                return "connection refused";
            case NetworkError::ConnectionReset:
                return "connection reset";
            case NetworkError::Timeout:
                return "operation timed out";
            case NetworkError::HostUnreachable:
                return "host unreachable";
            default:
                return "unknown network error (" + std::to_string(ev) + ")";
        }
    }
};

}  // anonymous namespace

// ===========================================================================
// Category singleton accessors
// ===========================================================================

const std::error_category& tox_error_category() noexcept {
    static const ToxErrorCategory instance;
    return instance;
}

const std::error_category& tunnel_error_category() noexcept {
    static const TunnelErrorCategory instance;
    return instance;
}

const std::error_category& network_error_category() noexcept {
    static const NetworkErrorCategory instance;
    return instance;
}

// ===========================================================================
// make_error_code overloads
// ===========================================================================

std::error_code make_error_code(ToxError e) noexcept {
    return {static_cast<int>(e), tox_error_category()};
}

std::error_code make_error_code(TunnelError e) noexcept {
    return {static_cast<int>(e), tunnel_error_category()};
}

std::error_code make_error_code(NetworkError e) noexcept {
    return {static_cast<int>(e), network_error_category()};
}

}  // namespace toxtunnel
