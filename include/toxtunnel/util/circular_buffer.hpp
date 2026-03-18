#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>

namespace toxtunnel::util {

/// Thread-safe circular (ring) buffer with a fixed capacity.
///
/// When the buffer is full, a new `write()` overwrites the oldest element
/// and advances the read head so that `size()` never exceeds `capacity()`.
///
/// All public operations are serialised with an internal mutex, making the
/// buffer safe to use from multiple threads concurrently.
template <typename T>
class CircularBuffer {
   public:
    /// Construct a circular buffer that can hold at most @p cap elements.
    /// @pre cap > 0
    explicit CircularBuffer(std::size_t cap)
        : buffer_(cap), capacity_(cap), head_(0), tail_(0), count_(0) {}

    // Non-copyable (mutex is non-copyable).
    CircularBuffer(const CircularBuffer&) = delete;
    CircularBuffer& operator=(const CircularBuffer&) = delete;

    // Movable only when no concurrent access is occurring.
    CircularBuffer(CircularBuffer&&) = default;
    CircularBuffer& operator=(CircularBuffer&&) = default;

    /// Write (push) an element into the buffer.
    ///
    /// If the buffer is full the oldest element is silently overwritten
    /// and the read head advances by one.
    void write(const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        write_impl(value);
    }

    /// @overload Move-enabled write.
    void write(T&& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        write_impl(std::move(value));
    }

    /// Read (pop) the oldest element from the buffer.
    ///
    /// @return The oldest element, or `std::nullopt` when the buffer is empty.
    [[nodiscard]] std::optional<T> read() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0) {
            return std::nullopt;
        }
        T value = std::move(buffer_[head_]);
        head_ = next(head_);
        --count_;
        return value;
    }

    /// Peek at the oldest element without removing it.
    ///
    /// @return A copy of the oldest element, or `std::nullopt` when empty.
    [[nodiscard]] std::optional<T> peek() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0) {
            return std::nullopt;
        }
        return buffer_[head_];
    }

    /// Peek at the element at the given logical index (0 = oldest).
    ///
    /// @return A copy of the element, or `std::nullopt` when @p index >= size().
    [[nodiscard]] std::optional<T> peek(std::size_t index) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index >= count_) {
            return std::nullopt;
        }
        return buffer_[(head_ + index) % capacity_];
    }

    /// Return the number of elements currently stored.
    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_;
    }

    /// Return the maximum number of elements the buffer can hold.
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    /// Return true when the buffer contains no elements.
    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ == 0;
    }

    /// Return true when the buffer is at capacity.
    [[nodiscard]] bool full() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return count_ == capacity_;
    }

    /// Discard all stored elements.  The capacity is unchanged.
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        tail_ = 0;
        count_ = 0;
    }

   private:
    /// Advance an index by one, wrapping around at the end of the
    /// underlying storage.
    [[nodiscard]] std::size_t next(std::size_t idx) const noexcept { return (idx + 1) % capacity_; }

    /// Shared write logic for both lvalue and rvalue overloads.
    template <typename U>
    void write_impl(U&& value) {
        buffer_[tail_] = std::forward<U>(value);
        tail_ = next(tail_);
        if (count_ == capacity_) {
            // Buffer was full -- the oldest element has been overwritten,
            // so the read head must advance past it.
            head_ = tail_;
        } else {
            ++count_;
        }
    }

    std::vector<T> buffer_;     ///< Underlying storage.
    std::size_t capacity_;      ///< Fixed maximum number of elements.
    std::size_t head_;          ///< Index of the oldest element (read position).
    std::size_t tail_;          ///< Index of the next write position.
    std::size_t count_;         ///< Current number of stored elements.
    mutable std::mutex mutex_;  ///< Guards all mutable state.
};

}  // namespace toxtunnel::util
