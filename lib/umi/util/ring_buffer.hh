/// @file ring_buffer.hh
/// @brief UMI Ring Buffer utilities
/// @author Shota Moriguchi @tekitounix
/// @date 2025
/// @license MIT

#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace umi {
namespace util {

/// @brief Single-producer single-consumer ring buffer
/// @tparam T Element type
/// @tparam N Buffer size (must be power of 2)
template <typename T, size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "RingBuffer size must be power of 2");
    
    std::array<T, N> buffer_;
    alignas(64) std::atomic<size_t> head_{0};  // Write index
    alignas(64) std::atomic<size_t> tail_{0};  // Read index
    
public:
    static constexpr size_t capacity = N;
    
    /// @brief Check if buffer is empty
    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }
    
    /// @brief Check if buffer is full
    [[nodiscard]] bool full() const noexcept {
        return ((head_.load(std::memory_order_acquire) + 1) & (N - 1)) == 
               tail_.load(std::memory_order_acquire);
    }
    
    /// @brief Get number of elements in buffer
    [[nodiscard]] size_t size() const noexcept {
        return (head_.load(std::memory_order_acquire) - 
                tail_.load(std::memory_order_acquire)) & (N - 1);
    }
    
    /// @brief Push element (producer only)
    bool push(const T& item) noexcept {
        const size_t h = head_.load(std::memory_order_relaxed);
        const size_t next = (h + 1) & (N - 1);
        
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;  // Full
        }
        
        buffer_[h] = item;
        head_.store(next, std::memory_order_release);
        return true;
    }
    
    /// @brief Pop element (consumer only)
    bool pop(T& item) noexcept {
        const size_t t = tail_.load(std::memory_order_relaxed);
        
        if (t == head_.load(std::memory_order_acquire)) {
            return false;  // Empty
        }
        
        item = buffer_[t];
        tail_.store((t + 1) & (N - 1), std::memory_order_release);
        return true;
    }
};

} // namespace util
} // namespace umi
