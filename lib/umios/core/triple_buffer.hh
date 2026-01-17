// SPDX-License-Identifier: MIT
// UMI-OS - Lock-free triple buffer for audio/UI communication

#pragma once

#include <atomic>
#include <array>
#include <cstdint>

namespace umi {

// ============================================================================
// Triple Buffer
// ============================================================================
//
// Lock-free triple buffer for single producer, single consumer scenarios.
// Ideal for UI → DSP or DSP → UI communication.
//
// Three buffers:
//   - Writer writes to "write" buffer, then publishes
//   - Reader reads from "read" buffer
//   - "Middle" buffer holds latest published data
//
// Benefits over double buffer:
//   - Writer never blocks
//   - Reader always gets latest complete data
//   - No tearing
//
// ============================================================================

template<typename T>
class TripleBuffer {
public:
    TripleBuffer() = default;
    
    /// Initialize all buffers with a value
    explicit TripleBuffer(const T& initial) {
        buffers_[0] = initial;
        buffers_[1] = initial;
        buffers_[2] = initial;
    }
    
    // Non-copyable
    TripleBuffer(const TripleBuffer&) = delete;
    TripleBuffer& operator=(const TripleBuffer&) = delete;
    
    // === Writer interface (producer thread) ===
    
    /// Get writable buffer reference
    [[nodiscard]] T& write_buffer() noexcept {
        return buffers_[write_idx_];
    }
    
    /// Publish written data (makes it available to reader)
    void publish() noexcept {
        // Swap write and middle indices atomically
        uint8_t old_middle = middle_idx_.exchange(write_idx_, std::memory_order_acq_rel);
        write_idx_ = old_middle;
        new_data_.store(true, std::memory_order_release);
    }
    
    // === Reader interface (consumer thread) ===
    
    /// Check if new data is available
    [[nodiscard]] bool has_new_data() const noexcept {
        return new_data_.load(std::memory_order_acquire);
    }
    
    /// Get readable buffer reference (call update() first if needed)
    [[nodiscard]] const T& read_buffer() const noexcept {
        return buffers_[read_idx_];
    }
    
    /// Update read buffer with latest published data
    /// Returns true if new data was available
    bool update() noexcept {
        if (!new_data_.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Swap read and middle indices atomically
        uint8_t old_middle = middle_idx_.exchange(read_idx_, std::memory_order_acq_rel);
        read_idx_ = old_middle;
        new_data_.store(false, std::memory_order_release);
        return true;
    }
    
    /// Update and get reference in one call
    [[nodiscard]] const T& read() noexcept {
        update();
        return buffers_[read_idx_];
    }
    
private:
    std::array<T, 3> buffers_{};
    
    uint8_t write_idx_ = 0;
    uint8_t read_idx_ = 2;
    std::atomic<uint8_t> middle_idx_{1};
    std::atomic<bool> new_data_{false};
};

// ============================================================================
// Triple Buffer for POD types (optimized, no atomics on data)
// ============================================================================

/// Convenience alias for parameter blocks
template<typename T>
using ParamBuffer = TripleBuffer<T>;

} // namespace umi
