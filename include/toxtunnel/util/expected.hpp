#pragma once

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace toxtunnel::util {

/// Tag wrapper for constructing an Expected in the error state.
/// Disambiguates the error constructor from the value constructor,
/// which is critical when T and E are the same type.
template <typename E>
class unexpected {
   public:
    explicit unexpected(const E& e) : error_(e) {}
    explicit unexpected(E&& e) : error_(std::move(e)) {}

    const E& value() const& { return error_; }
    E& value() & { return error_; }
    E&& value() && { return std::move(error_); }

   private:
    E error_;
};

/// Deduction guide so `unexpected(val)` deduces the template parameter.
template <typename E>
unexpected(E) -> unexpected<E>;

template <typename T, typename E>
class Expected {
   public:
    // Construct with value
    Expected(const T& value) : storage_(value) {}
    Expected(T&& value) : storage_(std::move(value)) {}

    // Construct with error via unexpected wrapper — resolves ambiguity when T == E
    Expected(const unexpected<E>& u) : storage_(u.value()) {}
    Expected(unexpected<E>&& u) : storage_(std::move(u).value()) {}

    // Check if has value
    [[nodiscard]] bool has_value() const { return std::holds_alternative<T>(storage_); }

    [[nodiscard]] explicit operator bool() const { return has_value(); }

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

    E&& error() && {
        if (has_value()) {
            throw std::runtime_error("Expected contains value");
        }
        return std::get<E>(std::move(storage_));
    }

    // Value or default
    T value_or(T&& default_value) const& {
        return has_value() ? value() : std::forward<T>(default_value);
    }

    T value_or(T&& default_value) && {
        return has_value() ? std::get<T>(std::move(storage_)) : std::forward<T>(default_value);
    }

   private:
    std::variant<T, E> storage_;
};

/// Helper function to create an Expected in the error state.
template <typename E>
unexpected<std::decay_t<E>> make_unexpected(E&& e) {
    return unexpected<std::decay_t<E>>(std::forward<E>(e));
}

}  // namespace toxtunnel::util
