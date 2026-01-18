// SPDX-License-Identifier: MIT
// UMI-USB: Audio Types and Common Components
// Shared between UAC1 and UAC2 implementations
#pragma once

#include <cstdint>
#include <span>
#include <array>

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
// PLL Rate Controller (for Adaptive mode)
// ============================================================================

/// Controls I2S clock rate based on buffer level
/// Target: maintain buffer at 50% fill to absorb USB jitter
class PllRateController {
public:
    // Buffer level targets (in frames)
    static constexpr int32_t TARGET_LEVEL = 128;  // 50% of 256
    static constexpr int32_t HYSTERESIS = 16;     // ±16 frames dead zone

    // PLL adjustment range (PPM from nominal)
    static constexpr int32_t MAX_PPM_ADJUST = 500;  // ±500ppm max

    void reset() {
        current_ppm_ = 0;
        integral_ = 0;
    }

    /// Update PLL based on buffer level
    /// Returns PPM adjustment (-500 to +500)
    /// Positive = speed up I2S clock, negative = slow down
    int32_t update(int32_t buffer_level) {
        int32_t error = buffer_level - TARGET_LEVEL;

        // Dead zone to avoid constant hunting
        if (error > -HYSTERESIS && error < HYSTERESIS) {
            return current_ppm_;
        }

        // PI controller
        // P term: immediate response
        int32_t p_term = error / 4;  // Scale down error

        // I term: slow drift correction
        integral_ += error;
        // Clamp integral to prevent windup
        if (integral_ > 10000) integral_ = 10000;
        if (integral_ < -10000) integral_ = -10000;
        int32_t i_term = integral_ / 100;

        // Calculate total adjustment
        current_ppm_ = p_term + i_term;

        // Clamp to safe range
        if (current_ppm_ > MAX_PPM_ADJUST) current_ppm_ = MAX_PPM_ADJUST;
        if (current_ppm_ < -MAX_PPM_ADJUST) current_ppm_ = -MAX_PPM_ADJUST;

        return current_ppm_;
    }

    [[nodiscard]] int32_t current_ppm() const { return current_ppm_; }

private:
    int32_t current_ppm_ = 0;
    int32_t integral_ = 0;
};

// ============================================================================
// Feedback Calculator (for Asynchronous mode)
// ============================================================================

/// Calculates feedback value for Asynchronous mode
/// UAC1: 10.14 fixed-point format (Full Speed)
/// UAC2: 16.16 fixed-point format
template<UacVersion Version = UacVersion::Uac1>
class FeedbackCalculator {
public:
    static constexpr uint32_t MEASUREMENT_WINDOW = 64;  // SOF frames to average

    // UAC1 FS: 10.14 format, UAC2: 16.16 format
    static constexpr uint32_t FEEDBACK_SHIFT = (Version == UacVersion::Uac1) ? 14 : 16;
    static constexpr uint32_t FEEDBACK_BYTES = (Version == UacVersion::Uac1) ? 3 : 4;

    void reset(uint32_t nominal_rate) {
        nominal_rate_ = nominal_rate;
        nominal_feedback_ = (nominal_rate << FEEDBACK_SHIFT) / 1000;
        current_feedback_ = nominal_feedback_;
        sample_count_ = 0;
        sof_count_ = 0;
        accumulated_samples_ = 0;
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
        if (sof_count_ > 0 && accumulated_samples_ > 0) {
            // Calculate actual rate: samples / frames * (1 << SHIFT)
            uint64_t measured = (static_cast<uint64_t>(accumulated_samples_) << FEEDBACK_SHIFT) / sof_count_;

            // Smooth the feedback to avoid sudden jumps
            // Use exponential moving average: new = old * 7/8 + measured * 1/8
            current_feedback_ = (current_feedback_ * 7 + static_cast<uint32_t>(measured)) / 8;

            // Clamp to ±1% of nominal to prevent runaway
            uint32_t min_fb = nominal_feedback_ * 99 / 100;
            uint32_t max_fb = nominal_feedback_ * 101 / 100;
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
};

// ============================================================================
// Ring Buffer for USB Audio
// ============================================================================

/// Lock-free SPSC ring buffer for USB <-> Audio DMA transfer
template<uint32_t Frames = 256, uint8_t Channels = 2>
class AudioRingBuffer {
public:
    static_assert((Frames & (Frames - 1)) == 0, "Frames must be power of 2");
    static constexpr uint32_t MASK = Frames - 1;
    static constexpr uint32_t PREBUFFER_FRAMES = Frames / 2;
    static constexpr uint32_t SAMPLES_PER_FRAME = Channels;
    static constexpr uint32_t BYTES_PER_FRAME = Channels * sizeof(int16_t);

    void reset() {
        write_pos_ = 0;
        read_pos_ = 0;
        playback_started_ = false;
        underrun_count_ = 0;
        overrun_count_ = 0;
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

        if constexpr (Channels == 2) {
            // Stereo: use 32-bit access for atomicity
            const uint32_t* src = reinterpret_cast<const uint32_t*>(samples);
            for (uint32_t i = 0; i < frame_count; ++i) {
                uint32_t idx = (write + i) & MASK;
                reinterpret_cast<uint32_t*>(buffer_)[idx] = src[i];
            }
        } else {
            // Mono or other channel counts
            for (uint32_t i = 0; i < frame_count; ++i) {
                uint32_t idx = (write + i) & MASK;
                for (uint8_t ch = 0; ch < Channels; ++ch) {
                    buffer_[idx * Channels + ch] = samples[i * Channels + ch];
                }
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

    [[nodiscard]] bool is_playback_started() const { return playback_started_; }
    [[nodiscard]] uint32_t buffered_frames() const {
        return (write_pos_ - read_pos_) & MASK;
    }
    [[nodiscard]] int32_t buffer_level() const {
        return static_cast<int32_t>(buffered_frames());
    }
    [[nodiscard]] uint32_t underrun_count() const { return underrun_count_; }
    [[nodiscard]] uint32_t overrun_count() const { return overrun_count_; }

private:
    alignas(32) int16_t buffer_[Frames * Channels]{};
    volatile uint32_t write_pos_ = 0;
    volatile uint32_t read_pos_ = 0;
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
