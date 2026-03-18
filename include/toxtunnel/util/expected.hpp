#pragma once

#include <variant>
#include <stdexcept>
#include <type_traits>

namespace toxtunnel::util {

template<typename T, typename E>
class Expected {
public:
    // Construct with value
    Expected(const T& value) : storage_(value) {}
    Expected(T&& value) : storage_(std::move(value)) {}

    // Construct with error
    Expected(const E& error) : storage_(error) {}
    Expected(E&& error) : storage_(std::move(error)) {}

    // Check if has value
    [[nodiscard]] bool has_value() const {
        return std::holds_alternative<T>(storage_);
    }

    [[nodiscard]] explicit operator bool() const {
        return has_value();
    }

    // Access value (throws if error)
    T& value() & {
        if (!has_value()) {
            throw std::runtime_error("Expected contains error");
        }
        return std::get<T>(storage_);
    }

    const T& value() const& {
        if (!has_value()) {
            throw std::runtime_error("Expected contains error");
        }
        return std::get<T>(storage_);
    }

    T&& value() && {
        if (!has_value()) {
            throw std::runtime_error("Expected contains error");
        }
        return std::get<T>(std::move(storage_));
    }

    // Access error (throws if value)
    E& error() & {
        if (has_value()) {
            throw std::runtime_error("Expected contains value");
        }
        return std::get<E>(storage_);
    }

    const E& error() const& {
        if (has_value()) {
            throw std::runtime_error("Expected contains value");
        }
        return std::get<E>(storage_);
    }

    // Value or default
    T value_or(T&& default_value) const& {
        return has_value() ? value() : std::forward<T>(default_value);
    }

private:
    std::variant<T, E> storage_;
};

}  // namespace toxtunnel::util
