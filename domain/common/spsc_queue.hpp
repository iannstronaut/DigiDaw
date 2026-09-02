#pragma once

#include <atomic>
#include <vector>
#include <cstddef>
#include <new>
#include <utility>

namespace digidaw::domain {

#if defined(__cpp_lib_hardware_interference_size)
    constexpr size_t hardware_destructive_interference_size = std::hardware_destructive_interference_size;
#else
    constexpr size_t hardware_destructive_interference_size = 64;
#endif

template <typename T, size_t Capacity = 1024>
class SPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    SPSCQueue() : head_(0), tail_(0) {
        buffer_ = reinterpret_cast<T*>(new std::byte[sizeof(T) * Capacity]);
    }

    ~SPSCQueue() {
        T dummy;
        while (pop(dummy)) {}
        delete[] reinterpret_cast<std::byte*>(buffer_);
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    // Real-time safe: non-blocking, wait-free, no alloc
    bool push(const T& item) noexcept(std::is_nothrow_copy_constructible_v<T>) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);

        if ((head - tail) >= Capacity) {
            return false; // Queue full
        }

        new (&buffer_[head & BufferMask]) T(item);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    bool push(T&& item) noexcept(std::is_nothrow_move_constructible_v<T>) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);

        if ((head - tail) >= Capacity) {
            return false; // Queue full
        }

        new (&buffer_[head & BufferMask]) T(std::move(item));
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Real-time safe: non-blocking, wait-free, no alloc
    bool pop(T& item) noexcept(std::is_nothrow_move_assignable_v<T>) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);

        if (tail == head) {
            return false; // Queue empty
        }

        T* elem = &buffer_[tail & BufferMask];
        item = std::move(*elem);
        elem->~T();
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        return tail == head;
    }

    [[nodiscard]] size_t size() const noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        return (head >= tail) ? (head - tail) : 0;
    }

    [[nodiscard]] constexpr size_t capacity() const noexcept {
        return Capacity;
    }

private:
    static constexpr size_t BufferMask = Capacity - 1;

    alignas(hardware_destructive_interference_size) std::atomic<size_t> head_;
    alignas(hardware_destructive_interference_size) std::atomic<size_t> tail_;
    alignas(hardware_destructive_interference_size) T* buffer_;
};

} // namespace digidaw::domain
