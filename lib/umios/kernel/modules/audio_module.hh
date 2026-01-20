// SPDX-License-Identifier: MIT
// umios Audio Module - Kernel module for audio processing
//
// Design principles:
// 1. ISRs only notify, never process
// 2. All processing happens in task context
// 3. Ring buffers decouple producers and consumers
// 4. Single owner per resource (no mutex needed)

#pragma once

#include <cstdint>
#include <cstring>

namespace umi::modules {

/// Audio ring buffer with atomic-like single-producer single-consumer access
/// No locks needed when one task writes and another reads
template<typename T, uint32_t Size>
class AudioRingBuffer {
    static_assert((Size & (Size - 1)) == 0, "Size must be power of 2");
    static constexpr uint32_t kMask = Size - 1;
    
    T buffer_[Size] = {};
    volatile uint32_t write_idx_ = 0;
    volatile uint32_t read_idx_ = 0;
    
public:
    void reset() {
        write_idx_ = 0;
        read_idx_ = 0;
    }
    
    uint32_t level() const {
        uint32_t w = write_idx_;
        uint32_t r = read_idx_;
        return (w >= r) ? (w - r) : (Size - r + w);
    }
    
    uint32_t available() const {
        return Size - 1 - level();
    }
    
    bool empty() const { return write_idx_ == read_idx_; }
    bool full() const { return ((write_idx_ + 1) & kMask) == read_idx_; }
    
    /// Write single sample (producer only)
    bool write(T sample) {
        uint32_t next = (write_idx_ + 1) & kMask;
        if (next == read_idx_) return false;  // Full
        buffer_[write_idx_] = sample;
        write_idx_ = next;
        return true;
    }
    
    /// Write multiple samples (producer only)
    uint32_t write(const T* data, uint32_t count) {
        uint32_t written = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (!write(data[i])) break;
            ++written;
        }
        return written;
    }
    
    /// Read single sample (consumer only)
    bool read(T& sample) {
        if (empty()) return false;
        sample = buffer_[read_idx_];
        read_idx_ = (read_idx_ + 1) & kMask;
        return true;
    }
    
    /// Read multiple samples (consumer only)
    uint32_t read(T* data, uint32_t count) {
        uint32_t read_count = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (!read(data[i])) break;
            ++read_count;
        }
        return read_count;
    }
    
    /// Peek without consuming (consumer only)
    bool peek(T& sample) const {
        if (empty()) return false;
        sample = buffer_[read_idx_];
        return true;
    }
    
    /// Skip samples (consumer only) - for drift correction
    uint32_t skip(uint32_t count) {
        uint32_t skipped = 0;
        for (uint32_t i = 0; i < count && !empty(); ++i) {
            read_idx_ = (read_idx_ + 1) & kMask;
            ++skipped;
        }
        return skipped;
    }
};

/// Audio module configuration
struct AudioModuleConfig {
    uint32_t sample_rate = 48000;
    uint32_t buffer_size = 64;      // Samples per DMA transfer
    uint32_t ring_size = 512;       // Ring buffer size
    uint32_t ring_target = 256;     // Target fill level
    uint32_t ring_high = 384;       // High watermark (skip)
    uint32_t ring_low = 128;        // Low watermark (duplicate)
};

/// Audio module events
namespace AudioEvent {
    constexpr uint32_t I2sComplete    = 1 << 0;  // I2S DMA buffer ready
    constexpr uint32_t PdmComplete    = 1 << 1;  // PDM DMA buffer ready
    constexpr uint32_t UsbSofTick     = 1 << 2;  // USB SOF (1ms) tick
    constexpr uint32_t MidiReceived   = 1 << 3;  // MIDI data available
    constexpr uint32_t BufferOverrun  = 1 << 4;  // Ring buffer overrun
    constexpr uint32_t BufferUnderrun = 1 << 5;  // Ring buffer underrun
}

/// Audio module statistics
struct AudioStats {
    uint32_t i2s_callbacks = 0;
    uint32_t pdm_callbacks = 0;
    uint32_t usb_sof_ticks = 0;
    uint32_t mic_underruns = 0;
    uint32_t mic_overruns = 0;
    uint32_t synth_underruns = 0;
    uint32_t synth_overruns = 0;
    uint32_t drift_skips = 0;
    uint32_t drift_dups = 0;
};

/// Audio processing task interface
/// Implement this for your specific audio pipeline
struct IAudioProcessor {
    virtual ~IAudioProcessor() = default;
    
    /// Called when I2S output buffer needs filling
    /// @param buffer Stereo interleaved buffer to fill
    /// @param frame_count Number of stereo frames
    virtual void on_i2s_output(int16_t* buffer, uint32_t frame_count) = 0;
    
    /// Called when PDM microphone data is ready
    /// @param pdm_data Raw PDM data from DMA
    /// @param word_count Number of 16-bit words
    virtual void on_pdm_input(const uint16_t* pdm_data, uint32_t word_count) = 0;
    
    /// Called at USB SOF rate (1ms) to prepare USB Audio IN
    /// @param buffer Stereo buffer for USB
    /// @param frame_count Number of stereo frames (typically 48)
    /// @return true if data was written
    virtual bool on_usb_sof(int16_t* buffer, uint32_t frame_count) = 0;
    
    /// Called when MIDI data is received
    /// @param data MIDI bytes
    /// @param len Number of bytes (1-4)
    virtual void on_midi(const uint8_t* data, uint32_t len) = 0;
};

}  // namespace umi::modules
