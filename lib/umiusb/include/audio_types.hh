// SPDX-License-Identifier: MIT
// UMI-USB: Audio Types and Common Components
// Shared between UAC1 and UAC2 implementations
#pragma once

#include <cstdint>
#include <span>
#include <array>
#include <umidsp/audio/rate/asrc.hh>

namespace umiusb {

// ============================================================================
// UAC Version
// ============================================================================

enum class UacVersion : uint8_t {
    Uac1 = 1,  // USB Audio Class 1.0 - widest compatibility
    Uac2 = 2,  // USB Audio Class 2.0 - high sample rates, low latency
};

// ============================================================================
// Audio Synchronization Mode
// ============================================================================

enum class AudioSyncMode : uint8_t {
    Async = 0x05,     // Asynchronous - device clock master, feedback EP required
    Adaptive = 0x09,  // Adaptive - device adapts to host rate
    Sync = 0x0D,      // Synchronous - locked to SOF (not recommended)
};

// ============================================================================
// Audio Direction
// ============================================================================

enum class AudioDirection : uint8_t {
    Out = 0,  // Host -> Device (speaker/playback)
    In = 1,   // Device -> Host (microphone/recording)
    Both = 2, // Bidirectional
};

// ============================================================================
// Type aliases for ASRC components from umidsp
// ============================================================================

// PI Rate Controller with USB Audio defaults (see lib/umidsp/include/asrc.hh)
using PllRateController = umidsp::UsbAudioPiController;

// ============================================================================
// Feedback Calculator (for Asynchronous mode)
// ============================================================================

/// Calculates feedback value for Asynchronous mode
/// UAC1: 10.14 fixed-point format (Full Speed)
/// UAC2: 16.16 fixed-point format
///
/// Based on TinyUSB's FIFO count method with proper low-pass filtering
template<UacVersion Version = UacVersion::Uac1>
class FeedbackCalculator {
public:
    // Use power-of-2 window for efficient shift-based calculation
    static constexpr uint32_t MEASUREMENT_WINDOW = 32;  // SOF frames to average
    static constexpr uint32_t WINDOW_SHIFT = 5;         // log2(32) = 5

    // UAC1 FS: 10.14 format, UAC2: 16.16 format
    static constexpr uint32_t FEEDBACK_SHIFT = (Version == UacVersion::Uac1) ? 14 : 16;
    static constexpr uint32_t FEEDBACK_BYTES = (Version == UacVersion::Uac1) ? 3 : 4;

    void reset(uint32_t nominal_rate) {
        nominal_rate_ = nominal_rate;
        // 10.14 format: feedback = samples_per_ms * 16384
        // For 48kHz: 48 * 16384 = 786432 = 0x0C0000
        nominal_feedback_ = (nominal_rate << FEEDBACK_SHIFT) / 1000;
        current_feedback_ = nominal_feedback_;
        sample_count_ = 0;
        sof_count_ = 0;
        accumulated_samples_ = 0;
        // Initialize FIFO level average (Q16 format) to target level
        fifo_level_avg_q16_ = 128 << 16;  // Target = 128 frames

        // Calculate TinyUSB-style rate constants
        // rate_const = (max_value - nominal) / fifo_threshold
        // With ±1% range and 128 frame threshold
        uint32_t max_fb = nominal_feedback_ + (nominal_feedback_ / 100);
        uint32_t min_fb = nominal_feedback_ - (nominal_feedback_ / 100);
        constexpr uint32_t fifo_threshold = 128;
        rate_const_up_ = (max_fb - nominal_feedback_) / fifo_threshold;
        rate_const_down_ = (nominal_feedback_ - min_fb) / fifo_threshold;
    }

    /// Set the actual device sample rate (for FIFO-based feedback)
    /// Use this when the device has a known fixed clock rate different from nominal
    /// @param actual_rate The actual sample rate in Hz (e.g., 47991 for STM32F4 I2S)
    ///
    /// This recalculates nominal_feedback_ AND rate_const_ based on the actual rate
    /// so that FIFO-based adjustments work correctly around the actual clock rate.
    void set_actual_rate(uint32_t actual_rate) {
        nominal_rate_ = actual_rate;
        // Calculate feedback value for the actual rate
        // UAC1 10.14 format: feedback = samples_per_ms * 16384
        // For 47991 Hz: 47.991 * 16384 = 786,300 = 0x0BFFCC
        nominal_feedback_ = (actual_rate << FEEDBACK_SHIFT) / 1000;
        current_feedback_ = nominal_feedback_;

        // Recalculate rate constants based on actual rate
        // TinyUSB formula: rate_const = (max_value - nominal) / fifo_threshold
        // ±1% range around the actual rate
        uint32_t max_fb = nominal_feedback_ + (nominal_feedback_ / 100);
        uint32_t min_fb = nominal_feedback_ - (nominal_feedback_ / 100);
        constexpr uint32_t fifo_threshold = 128;
        rate_const_up_ = (max_fb - nominal_feedback_) / fifo_threshold;
        rate_const_down_ = (nominal_feedback_ - min_fb) / fifo_threshold;

        // Initialize FIFO level average to target
        fifo_level_avg_q16_ = 128 << 16;
    }

    /// Call this every SOF (1ms for FS, 125us for HS) to update timing
    void on_sof() {
        sof_count_++;
        if (sof_count_ >= MEASUREMENT_WINDOW) {
            update_feedback();
        }
    }

    /// Call this when samples are consumed by I2S DMA
    void add_consumed_samples(uint32_t count) {
        accumulated_samples_ += count;
    }

    /// Update feedback based on FIFO level (TinyUSB FIFO count method)
    /// Call this periodically to adjust feedback based on buffer fill level
    /// @param current_level Current FIFO fill level (frames)
    /// @param target_level Target FIFO fill level (frames), typically half buffer
    ///
    /// Algorithm from TinyUSB audiod_fb_fifo_count_update():
    /// 1. Apply 64-point low-pass filter to smooth FIFO level readings
    /// 2. Compare filtered level to threshold
    /// 3. Apply linear adjustment based on deviation
    void update_from_fifo_level(int32_t current_level, int32_t target_level) {
        // TinyUSB-style low-pass filter (64-point averaging)
        // lvl = (lvl * 63 + (lvl_new << 16)) >> 6
        // This is a Q16 fixed-point exponential moving average
        uint32_t lvl_new_q16 = static_cast<uint32_t>(current_level) << 16;
        fifo_level_avg_q16_ = ((fifo_level_avg_q16_ * 63) + lvl_new_q16) >> 6;

        // Extract integer part of filtered level
        uint32_t filtered_level = fifo_level_avg_q16_ >> 16;
        uint32_t threshold = static_cast<uint32_t>(target_level);

        // Calculate feedback adjustment based on deviation from target
        // TinyUSB approach: linear adjustment based on frame count difference
        if (filtered_level < threshold) {
            // Buffer underfilled: increase feedback to request more data from host
            uint32_t deficit = threshold - filtered_level;
            current_feedback_ = nominal_feedback_ + (deficit * rate_const_up_);
        } else {
            // Buffer overfilled: decrease feedback to request less data from host
            uint32_t excess = filtered_level - threshold;
            current_feedback_ = nominal_feedback_ - (excess * rate_const_down_);
        }

        // Clamp to ±1% of nominal to prevent runaway
        uint32_t min_fb = nominal_feedback_ - (nominal_feedback_ / 100);
        uint32_t max_fb = nominal_feedback_ + (nominal_feedback_ / 100);
        if (current_feedback_ < min_fb) current_feedback_ = min_fb;
        if (current_feedback_ > max_fb) current_feedback_ = max_fb;
    }

    /// Get current feedback value
    [[nodiscard]] uint32_t get_feedback() const {
        return current_feedback_;
    }

    /// Get feedback as byte array for USB transfer
    [[nodiscard]] auto get_feedback_bytes() const {
        if constexpr (Version == UacVersion::Uac1) {
            return std::array<uint8_t, 3>{
                static_cast<uint8_t>(current_feedback_ & 0xFF),
                static_cast<uint8_t>((current_feedback_ >> 8) & 0xFF),
                static_cast<uint8_t>((current_feedback_ >> 16) & 0xFF),
            };
        } else {
            return std::array<uint8_t, 4>{
                static_cast<uint8_t>(current_feedback_ & 0xFF),
                static_cast<uint8_t>((current_feedback_ >> 8) & 0xFF),
                static_cast<uint8_t>((current_feedback_ >> 16) & 0xFF),
                static_cast<uint8_t>((current_feedback_ >> 24) & 0xFF),
            };
        }
    }

    /// Get feedback rate as float (for debugging)
    [[nodiscard]] float get_feedback_rate() const {
        return static_cast<float>(current_feedback_) / (1 << FEEDBACK_SHIFT);
    }

private:
    void update_feedback() {
        if (accumulated_samples_ > 0) {
            // Calculate feedback using shift instead of division
            // measured = (samples << FEEDBACK_SHIFT) / WINDOW
            //          = samples << (FEEDBACK_SHIFT - WINDOW_SHIFT)
            uint32_t measured = accumulated_samples_ << (FEEDBACK_SHIFT - WINDOW_SHIFT);

            // Smooth the feedback to avoid sudden jumps
            // Use exponential moving average: new = old * 7/8 + measured * 1/8
            // Implemented with shifts: (old * 7 + measured) >> 3
            current_feedback_ = ((current_feedback_ * 7) + measured) >> 3;

            // Clamp to ±1% of nominal to prevent runaway
            uint32_t min_fb = nominal_feedback_ - (nominal_feedback_ / 100);
            uint32_t max_fb = nominal_feedback_ + (nominal_feedback_ / 100);
            if (current_feedback_ < min_fb) current_feedback_ = min_fb;
            if (current_feedback_ > max_fb) current_feedback_ = max_fb;
        }
        sof_count_ = 0;
        accumulated_samples_ = 0;
    }

    uint32_t nominal_rate_ = 48000;
    uint32_t nominal_feedback_ = 0;
    uint32_t current_feedback_ = 0;
    uint32_t sample_count_ = 0;
    uint32_t sof_count_ = 0;
    uint32_t accumulated_samples_ = 0;
    uint32_t fifo_level_avg_q16_ = 128 << 16;  // Q16 filtered FIFO level

    // TinyUSB-style rate constants for FIFO-based feedback
    // Calculated as: rate_const = (max_value - nominal) / fifo_threshold
    // For 48kHz, ±1% range, 128 frame threshold: rate_const ≈ 61
    uint32_t rate_const_up_ = 61;    // When buffer underfilled
    uint32_t rate_const_down_ = 61;  // When buffer overfilled
};


// ============================================================================
// Ring Buffer for USB Audio
// ============================================================================

/// Lock-free SPSC ring buffer for USB <-> Audio DMA transfer
/// Supports optional cubic hermite interpolation for ASRC
template<uint32_t Frames = 256, uint8_t Channels = 2>
class AudioRingBuffer {
public:
    static_assert((Frames & (Frames - 1)) == 0, "Frames must be power of 2");
    static constexpr uint32_t MASK = Frames - 1;
    // Lower prebuffer for reduced latency (was Frames/2, now Frames/4)
    static constexpr uint32_t PREBUFFER_FRAMES = Frames / 4;
    static constexpr uint32_t SAMPLES_PER_FRAME = Channels;
    static constexpr uint32_t BYTES_PER_FRAME = Channels * sizeof(int16_t);

    void reset() {
        write_pos_ = 0;
        read_pos_ = 0;
        read_frac_ = 0;
        playback_started_ = false;
        underrun_count_ = 0;
        overrun_count_ = 0;
    }

    /// Reset and immediately enable reading (for Audio IN - no prebuffer needed)
    void reset_and_start() {
        reset();
        playback_started_ = true;
    }

    /// Write frames from USB callback (producer)
    uint32_t write(const int16_t* samples, uint32_t frame_count) {
        uint32_t write = write_pos_;
        uint32_t read = read_pos_;
        uint32_t available = (write - read) & MASK;
        uint32_t free_space = Frames - 1 - available;

        if (frame_count > free_space) {
            frame_count = free_space;
            overrun_count_++;
        }

        if (frame_count > 0) {
            uint32_t write_idx = write & MASK;
            uint32_t first_chunk = Frames - write_idx;  // Frames until wrap

            if (frame_count <= first_chunk) {
                // No wrap: single memcpy
                __builtin_memcpy(&buffer_[write_idx * Channels], samples,
                                frame_count * BYTES_PER_FRAME);
            } else {
                // Wrap: two memcpy calls
                __builtin_memcpy(&buffer_[write_idx * Channels], samples,
                                first_chunk * BYTES_PER_FRAME);
                __builtin_memcpy(buffer_, &samples[static_cast<size_t>(first_chunk) * Channels],
                                (frame_count - first_chunk) * BYTES_PER_FRAME);
            }
        }

        asm volatile("dmb" ::: "memory");
        write_pos_ = (write + frame_count) & MASK;

        if (!playback_started_) {
            uint32_t buffered = (write_pos_ - read_pos_) & MASK;
            if (buffered >= PREBUFFER_FRAMES) {
                playback_started_ = true;
            }
        }

        return frame_count;
    }

    /// Read frames for Audio DMA (consumer)
    uint32_t read(int16_t* dest, uint32_t frame_count) {
        if (!playback_started_) {
            __builtin_memset(dest, 0, static_cast<size_t>(frame_count) * BYTES_PER_FRAME);
            return 0;
        }

        asm volatile("dmb" ::: "memory");
        uint32_t write = write_pos_;
        uint32_t read = read_pos_;
        uint32_t available = (write - read) & MASK;

        // Calculate how many frames we can actually read
        uint32_t to_read = (frame_count < available) ? frame_count : available;

        if (to_read > 0) {
            uint32_t read_idx = read & MASK;
            uint32_t first_chunk = Frames - read_idx;  // Frames until wrap

            if constexpr (Channels == 2) {
                // Stereo: batch copy as 32-bit words
                auto* dst = reinterpret_cast<uint32_t*>(dest);
                const auto* src = reinterpret_cast<const uint32_t*>(buffer_);

                if (to_read <= first_chunk) {
                    // No wrap: single copy
                    __builtin_memcpy(dst, src + read_idx, to_read * sizeof(uint32_t));
                } else {
                    // Wrap: two copies
                    __builtin_memcpy(dst, src + read_idx, first_chunk * sizeof(uint32_t));
                    __builtin_memcpy(dst + first_chunk, src, (to_read - first_chunk) * sizeof(uint32_t));
                }
            } else {
                // Mono or other channel counts
                if (to_read <= first_chunk) {
                    __builtin_memcpy(dest, buffer_ + (read_idx * Channels), to_read * BYTES_PER_FRAME);
                } else {
                    __builtin_memcpy(dest, buffer_ + (read_idx * Channels), first_chunk * BYTES_PER_FRAME);
                    __builtin_memcpy(dest + (first_chunk * Channels), buffer_, (to_read - first_chunk) * BYTES_PER_FRAME);
                }
            }
            read_pos_ = (read + to_read) & MASK;
        }

        // Zero-fill any remaining frames (underrun)
        if (to_read < frame_count) {
            uint32_t underrun_frames = frame_count - to_read;
            __builtin_memset(dest + (to_read * Channels), 0, static_cast<size_t>(underrun_frames) * BYTES_PER_FRAME);
            underrun_count_ += underrun_frames;
        }

        return to_read;
    }

    /// Read frames with cubic hermite interpolation (ASRC)
    /// rate_q16: Q16.16 fixed-point rate ratio (0x10000 = 1.0)
    ///   > 0x10000: consume more input (speed up output)
    ///   < 0x10000: consume fewer input (slow down output)
    /// Returns actual frames output (always == frame_count if enough data)
    uint32_t read_interpolated(int16_t* dest, uint32_t frame_count, uint32_t rate_q16) {
        if (!playback_started_) {
            __builtin_memset(dest, 0, static_cast<size_t>(frame_count) * BYTES_PER_FRAME);
            return 0;
        }

        asm volatile("dmb" ::: "memory");
        uint32_t write = write_pos_;
        uint32_t read = read_pos_;
        uint32_t available = (write - read) & MASK;

        // Need at least 4 frames for cubic interpolation (y0, y1, y2, y3)
        if (available < 4) {
            __builtin_memset(dest, 0, static_cast<size_t>(frame_count) * BYTES_PER_FRAME);
            underrun_count_ += frame_count;
            return 0;
        }

        uint32_t out_frames = 0;
        uint32_t frac = read_frac_;

        while (out_frames < frame_count) {
            // Check if we have enough samples (need at least 3 ahead: y1, y2, y3)
            uint32_t cur_read = read_pos_;
            uint32_t cur_available = (write - cur_read) & MASK;
            if (cur_available < 4) {
                break;  // Not enough data
            }

            // Get 4 sample positions for interpolation
            uint32_t idx0 = cur_read & MASK;
            uint32_t idx1 = (cur_read + 1) & MASK;
            uint32_t idx2 = (cur_read + 2) & MASK;
            uint32_t idx3 = (cur_read + 3) & MASK;

            // Interpolate each channel
            for (uint8_t ch = 0; ch < Channels; ++ch) {
                int16_t s0 = buffer_[idx0 * Channels + ch];
                int16_t s1 = buffer_[idx1 * Channels + ch];
                int16_t s2 = buffer_[idx2 * Channels + ch];
                int16_t s3 = buffer_[idx3 * Channels + ch];

                dest[out_frames * Channels + ch] =
                    umidsp::cubic_hermite::interpolate_i16(s0, s1, s2, s3, frac);
            }
            out_frames++;

            // Advance fractional position
            frac += rate_q16;
            uint32_t consumed = frac >> 16;
            frac &= 0xFFFF;

            // Advance read position by consumed whole samples
            read_pos_ = (cur_read + consumed) & MASK;
        }

        read_frac_ = frac;

        // Zero-fill any remaining frames (underrun)
        if (out_frames < frame_count) {
            uint32_t underrun_frames = frame_count - out_frames;
            __builtin_memset(dest + (out_frames * Channels), 0,
                            static_cast<size_t>(underrun_frames) * BYTES_PER_FRAME);
            underrun_count_ += underrun_frames;
        }

        return out_frames;
    }

    /// Set ASRC rate from PPM adjustment
    /// ppm: parts per million adjustment (-500 to +500 typical)
    /// Positive ppm = device clock is fast, need to consume more input
    [[nodiscard]] static uint32_t ppm_to_rate_q16(int32_t ppm) {
        // rate = 1.0 + ppm/1000000
        // In Q16.16: rate = 0x10000 + (0x10000 * ppm) / 1000000
        //                 = 0x10000 + (ppm * 65536) / 1000000
        //                 = 0x10000 + (ppm * 65536 + 500000) / 1000000  (rounded)
        // Simplified: rate ≈ 0x10000 + ppm / 15  (approximation for small ppm)
        return static_cast<uint32_t>(0x10000 + (ppm * 65536 + 500000) / 1000000);
    }

    [[nodiscard]] bool is_playback_started() const { return playback_started_; }
    void start_playback() { playback_started_ = true; }
    [[nodiscard]] uint32_t buffered_frames() const {
        return (write_pos_ - read_pos_) & MASK;
    }
    [[nodiscard]] int32_t buffer_level() const {
        return static_cast<int32_t>(buffered_frames());
    }
    [[nodiscard]] uint32_t underrun_count() const { return underrun_count_; }
    [[nodiscard]] uint32_t overrun_count() const { return overrun_count_; }
    
    // Debug: get raw sample at buffer index
    [[nodiscard]] int16_t dbg_sample_at(uint32_t idx) const {
        return buffer_[idx];
    }
    [[nodiscard]] uint32_t dbg_write_pos() const { return write_pos_; }
    [[nodiscard]] uint32_t dbg_read_pos() const { return read_pos_; }

private:
    alignas(32) int16_t buffer_[Frames * Channels]{};
    volatile uint32_t write_pos_ = 0;
    volatile uint32_t read_pos_ = 0;
    uint32_t read_frac_ = 0;  // Fractional read position (Q0.16)
    volatile bool playback_started_ = false;
    uint32_t underrun_count_ = 0;
    uint32_t overrun_count_ = 0;
};

// ============================================================================
// MIDI Processing
// ============================================================================

/// USB MIDI packet processing (shared between UAC1 and UAC2)
class MidiProcessor {
public:
    using MidiCallback = void(*)(uint8_t cable, const uint8_t* data, uint8_t len);
    using SysExCallback = void(*)(const uint8_t* data, uint16_t len);

    MidiCallback on_midi = nullptr;
    SysExCallback on_sysex = nullptr;

    void process_packet(uint8_t header, uint8_t b0, uint8_t b1, uint8_t b2) {
        uint8_t cin = header & 0x0F;
        uint8_t cable = header >> 4;

        switch (cin) {
            case 0x04:  // SysEx start/continue
                if (!in_sysex_) {
                    in_sysex_ = true;
                    sysex_pos_ = 0;
                }
                if (sysex_pos_ + 3 <= sysex_buf_.size()) {
                    sysex_buf_[sysex_pos_++] = b0;
                    sysex_buf_[sysex_pos_++] = b1;
                    sysex_buf_[sysex_pos_++] = b2;
                }
                break;

            case 0x05:  // SysEx ends with 1 byte
                if (sysex_pos_ < sysex_buf_.size()) {
                    sysex_buf_[sysex_pos_++] = b0;
                }
                complete_sysex();
                break;

            case 0x06:  // SysEx ends with 2 bytes
                if (sysex_pos_ + 2 <= sysex_buf_.size()) {
                    sysex_buf_[sysex_pos_++] = b0;
                    sysex_buf_[sysex_pos_++] = b1;
                }
                complete_sysex();
                break;

            case 0x07:  // SysEx ends with 3 bytes
                if (sysex_pos_ + 3 <= sysex_buf_.size()) {
                    sysex_buf_[sysex_pos_++] = b0;
                    sysex_buf_[sysex_pos_++] = b1;
                    sysex_buf_[sysex_pos_++] = b2;
                }
                complete_sysex();
                break;

            case 0x08:  // Note Off
            case 0x09:  // Note On
            case 0x0A:  // Poly Aftertouch
            case 0x0B:  // Control Change
            case 0x0E:  // Pitch Bend
                if (on_midi != nullptr) {
                    std::array<uint8_t, 3> msg = {b0, b1, b2};
                    on_midi(cable, msg.data(), 3);
                }
                break;

            case 0x0C:  // Program Change
            case 0x0D:  // Channel Aftertouch
                if (on_midi != nullptr) {
                    std::array<uint8_t, 2> msg = {b0, b1};
                    on_midi(cable, msg.data(), 2);
                }
                break;

            default:
                break;
        }
    }

    static constexpr uint8_t status_to_cin(uint8_t status) {
        switch (status & 0xF0) {
            case 0x80: return 0x08;
            case 0x90: return 0x09;
            case 0xA0: return 0x0A;
            case 0xB0: return 0x0B;
            case 0xC0: return 0x0C;
            case 0xD0: return 0x0D;
            case 0xE0: return 0x0E;
            case 0xF0: return 0x04;
            default: return 0x0F;
        }
    }

private:
    void complete_sysex() {
        if (on_sysex != nullptr && sysex_pos_ > 0) {
            on_sysex(sysex_buf_.data(), sysex_pos_);
        }
        in_sysex_ = false;
        sysex_pos_ = 0;
    }

    std::array<uint8_t, 256> sysex_buf_{};
    uint16_t sysex_pos_ = 0;
    bool in_sysex_ = false;
};

// ============================================================================
// UAC Descriptor Constants
// ============================================================================

namespace uac {

// Descriptor subtypes - Audio Control
namespace ac {
    inline constexpr uint8_t HEADER = 0x01;
    inline constexpr uint8_t INPUT_TERMINAL = 0x02;
    inline constexpr uint8_t OUTPUT_TERMINAL = 0x03;
    inline constexpr uint8_t MIXER_UNIT = 0x04;
    inline constexpr uint8_t SELECTOR_UNIT = 0x05;
    inline constexpr uint8_t FEATURE_UNIT = 0x06;
    inline constexpr uint8_t EFFECT_UNIT = 0x07;       // UAC2
    inline constexpr uint8_t PROCESSING_UNIT = 0x08;
    inline constexpr uint8_t EXTENSION_UNIT = 0x09;
    inline constexpr uint8_t CLOCK_SOURCE = 0x0A;      // UAC2
    inline constexpr uint8_t CLOCK_SELECTOR = 0x0B;    // UAC2
    inline constexpr uint8_t CLOCK_MULTIPLIER = 0x0C;  // UAC2
    inline constexpr uint8_t SAMPLE_RATE_CONVERTER = 0x0D;  // UAC2
}

// Descriptor subtypes - Audio Streaming
namespace as {
    inline constexpr uint8_t GENERAL = 0x01;
    inline constexpr uint8_t FORMAT_TYPE = 0x02;
    inline constexpr uint8_t ENCODER = 0x03;           // UAC2
    inline constexpr uint8_t DECODER = 0x04;           // UAC2
}

// Descriptor subtypes - MIDI Streaming
namespace ms {
    inline constexpr uint8_t HEADER = 0x01;
    inline constexpr uint8_t MIDI_IN_JACK = 0x02;
    inline constexpr uint8_t MIDI_OUT_JACK = 0x03;
    inline constexpr uint8_t ELEMENT = 0x04;
    inline constexpr uint8_t GENERAL = 0x01;  // For endpoint
}

// Terminal types
inline constexpr uint16_t TERMINAL_USB_STREAMING = 0x0101;
inline constexpr uint16_t TERMINAL_SPEAKER = 0x0301;
inline constexpr uint16_t TERMINAL_HEADPHONES = 0x0302;
inline constexpr uint16_t TERMINAL_MICROPHONE = 0x0201;
inline constexpr uint16_t TERMINAL_LINE_IN = 0x0501;
inline constexpr uint16_t TERMINAL_LINE_OUT = 0x0602;

// Format types
inline constexpr uint8_t FORMAT_TYPE_I = 0x01;
inline constexpr uint8_t FORMAT_TYPE_II = 0x02;
inline constexpr uint8_t FORMAT_TYPE_III = 0x03;

// Audio data formats
inline constexpr uint16_t FORMAT_PCM = 0x0001;
inline constexpr uint16_t FORMAT_PCM8 = 0x0002;
inline constexpr uint16_t FORMAT_IEEE_FLOAT = 0x0003;

// MIDI jack types
inline constexpr uint8_t JACK_EMBEDDED = 0x01;
inline constexpr uint8_t JACK_EXTERNAL = 0x02;

// UAC2 specific
namespace uac2 {
    // Clock source attributes
    inline constexpr uint8_t CLOCK_EXTERNAL = 0x00;
    inline constexpr uint8_t CLOCK_INTERNAL_FIXED = 0x01;
    inline constexpr uint8_t CLOCK_INTERNAL_VARIABLE = 0x02;
    inline constexpr uint8_t CLOCK_INTERNAL_PROGRAMMABLE = 0x03;
    inline constexpr uint8_t CLOCK_SYNCED_TO_SOF = 0x04;

    // Audio function category
    inline constexpr uint8_t FUNCTION_SUBCLASS = 0x00;

    // Interface protocol
    inline constexpr uint8_t IP_VERSION_02_00 = 0x20;
}

}  // namespace uac

}  // namespace umiusb
