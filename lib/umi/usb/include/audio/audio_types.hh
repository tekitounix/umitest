// SPDX-License-Identifier: MIT
// UMI-USB: Audio Types and Common Components
// Shared between UAC1 and UAC2 implementations
#pragma once

#include <array>
#include <atomic>
#include <audio/rate/asrc.hh>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>

#include "core/types.hh"

namespace umiusb {

// ============================================================================
// Sample Rate Lists (compile-time)
// ============================================================================

namespace detail {
template <size_t N>
constexpr uint32_t max_rate(const std::array<uint32_t, N>& rates) {
    uint32_t max_val = 0;
    for (uint32_t rate : rates) {
        if (rate > max_val)
            max_val = rate;
    }
    return max_val;
}

template <size_t N>
constexpr uint32_t min_rate(const std::array<uint32_t, N>& rates) {
    if constexpr (N == 0) {
        return 0;
    } else {
        uint32_t min_val = rates[0];
        for (uint32_t rate : rates) {
            if (rate < min_val)
                min_val = rate;
        }
        return min_val;
    }
}
} // namespace detail

template <uint32_t... Rates>
struct AudioRates {
    static constexpr std::array<uint32_t, sizeof...(Rates)> values = {Rates...};
    static constexpr size_t count = sizeof...(Rates);
    static constexpr uint32_t max_rate = detail::max_rate(values);
    static constexpr uint32_t min_rate = detail::min_rate(values);
};

// ============================================================================
// Alternate Format Settings (UAC1 streaming interfaces)
// ============================================================================

template <uint8_t BitDepth_, typename Rates_>
struct AudioAltSetting {
    static constexpr uint8_t BIT_DEPTH = BitDepth_;
    using Rates = Rates_;
    static constexpr size_t RATES_COUNT = Rates::count;
    static constexpr auto RATES = Rates::values;
    static constexpr uint32_t MAX_RATE = Rates::max_rate;
};

template <typename... AltSettings>
struct AudioAltList {
    static constexpr size_t count = sizeof...(AltSettings);

    template <size_t Index>
    using at = std::tuple_element_t<Index, std::tuple<AltSettings...>>;

    static constexpr uint32_t max_rate = []() {
        uint32_t max_val = 0;
        ((max_val = (AltSettings::MAX_RATE > max_val) ? AltSettings::MAX_RATE : max_val), ...);
        return max_val;
    }();
};

template <uint8_t BitDepth_, typename Rates_>
using DefaultAltList = AudioAltList<AudioAltSetting<BitDepth_, Rates_>>;

// ============================================================================
// UAC Version
// ============================================================================

enum class UacVersion : uint8_t {
    UAC1 = 1, // USB Audio Class 1.0 - widest compatibility
    UAC2 = 2, // USB Audio Class 2.0 - high sample rates, low latency
};

// ============================================================================
// Audio Synchronization Mode
// ============================================================================

enum class AudioSyncMode : uint8_t {
    ASYNC = 0x05,    // Asynchronous - device clock master, feedback EP required
    ADAPTIVE = 0x09, // Adaptive - device adapts to host rate
    SYNC = 0x0D,     // Synchronous - locked to SOF (not recommended)
};

// ============================================================================
// Audio Direction
// ============================================================================

enum class AudioDirection : uint8_t {
    OUT = 0,  // Host -> Device (speaker/playback)
    IN = 1,   // Device -> Host (microphone/recording)
    BOTH = 2, // Bidirectional
};

// ============================================================================
// Type aliases for ASRC components from umidsp
// ============================================================================

// PI Rate Controller (runtime configurable for buffer size)
using PllRateController = umidsp::PiRateController;

// ============================================================================
// Feedback Calculator (for Asynchronous mode)
// ============================================================================

/// Calculates feedback value for Asynchronous mode.
/// Full Speed uses 10.14 fixed-point format (3 bytes) per USB 2.0 §5.12.4.2.
/// Note: UAC2 spec says 16.16 (4 bytes) but macOS xHCI babbles with wMaxPacketSize > 3 at FS.
/// Algorithm based on STM32F401_USB_AUDIO_DAC reference (PID-style buffer tracking).
///
/// The host adjusts its packet size based on the feedback value to keep the
/// device's ring buffer at ~50% fill level.
template <UacVersion Version = UacVersion::UAC1, Speed Spd = Speed::FULL>
class FeedbackCalculator {
  public:
    using Traits = SpeedTraits<Spd>;

    // Feedback format depends on speed:
    //   FS: 10.14 format, 3 bytes (macOS xHCI babble with >3 bytes at FS)
    //   HS: 16.16 format, 4 bytes
    static constexpr uint32_t FEEDBACK_SHIFT = Traits::FB_SHIFT;
    static constexpr uint32_t FEEDBACK_BYTES = Traits::FB_BYTES;

    // Feedback clamp: ±1 sample/frame from nominal
    static constexpr uint32_t FB_DELTA_MAX = 1U << FEEDBACK_SHIFT;

    void reset(uint32_t nominal_rate) {
        nominal_rate_ = nominal_rate;
        nominal_feedback_ = (nominal_rate << FEEDBACK_SHIFT) / Traits::FRAME_DIVISOR;
        current_feedback_ = nominal_feedback_;
        buf_half_size_ = 0;
    }

    /// Set the buffer half-size (nominal fill target in samples).
    /// This is AUDIO_TOTAL_BUF_SIZE / (2 * bytes_per_stereo_sample) in reference terms.
    void set_buffer_half_size(uint32_t half_size_samples) {
        buf_half_size_ = half_size_samples;
    }

    /// Set the actual device sample rate (when device clock differs from nominal).
    void set_actual_rate(uint32_t actual_rate) {
        nominal_rate_ = actual_rate;
        nominal_feedback_ = (actual_rate << FEEDBACK_SHIFT) / Traits::FRAME_DIVISOR;
        current_feedback_ = nominal_feedback_;
    }

    /// Update feedback from buffer fill level.
    /// Call every 2 SOFs (every 2ms) from the SOF handler.
    ///
    /// @param writable_samples Number of writable (free) sample-frames in the ring buffer.
    ///
    /// Algorithm from STM32F401_USB_AUDIO_DAC:
    ///   deviation = writable - half_size  (positive = underfilled, negative = overfilled)
    ///   tmp = (1 << 22) + (deviation * PID_GAIN)
    ///   fb = (nominal * tmp) >> 22
    ///   clamp to ±1 kHz
    void update_from_buffer_level(int32_t writable_samples) {
        if (buf_half_size_ == 0)
            return;

        int32_t deviation = writable_samples - static_cast<int32_t>(buf_half_size_);

        // PID gain: 256 per sample deviation (from reference)
        // This provides ~1 sample/frame correction per deviation sample
        constexpr int32_t PID_GAIN = 256;
        int64_t tmp = (1LL << 22) + (static_cast<int64_t>(deviation) * PID_GAIN);

        // Prevent negative multiplier (would invert feedback direction)
        if (tmp < 0)
            tmp = 0;

        uint64_t pid_result = static_cast<uint64_t>(nominal_feedback_) * static_cast<uint64_t>(tmp);
        uint32_t fb = static_cast<uint32_t>(pid_result >> 22);

        // Clamp to ±1 sample/frame from nominal
        uint32_t fb_max = nominal_feedback_ + FB_DELTA_MAX;
        uint32_t fb_min = (nominal_feedback_ > FB_DELTA_MAX) ? (nominal_feedback_ - FB_DELTA_MAX) : 0;
        if (fb > fb_max)
            fb = fb_max;
        if (fb < fb_min)
            fb = fb_min;

        current_feedback_ = fb;
    }

    /// Get current feedback value (Q10.14)
    [[nodiscard]] uint32_t get_feedback() const { return current_feedback_; }

    /// Get feedback as 3-byte array for USB transfer (10.14 format, little-endian).
    [[nodiscard]] std::array<uint8_t, FEEDBACK_BYTES> get_feedback_bytes() const {
        uint32_t fb = current_feedback_ & 0x00FFFFFFu;
        return {
            static_cast<uint8_t>(fb & 0xFF),
            static_cast<uint8_t>((fb >> 8) & 0xFF),
            static_cast<uint8_t>((fb >> 16) & 0xFF),
        };
    }

    /// Get feedback rate as float (for debugging)
    [[nodiscard]] float get_feedback_rate() const {
        return static_cast<float>(current_feedback_) / (1 << FEEDBACK_SHIFT);
    }

  private:
    uint32_t nominal_rate_ = 48000;
    uint32_t nominal_feedback_ = 0;
    uint32_t current_feedback_ = 0;
    uint32_t buf_half_size_ = 0; // Target: half of ring buffer capacity in frames
};

// ============================================================================
// Ring Buffer for USB Audio
// ============================================================================

/// Lock-free SPSC ring buffer for USB <-> Audio DMA transfer
/// Supports optional cubic hermite interpolation for ASRC
template <uint32_t Frames = 256, uint8_t Channels = 2, typename SampleT = int32_t>
class AudioRingBuffer {
  public:
    static_assert((Frames & (Frames - 1)) == 0, "Frames must be power of 2");
    static constexpr uint32_t MASK = Frames - 1;
    static constexpr uint32_t SAMPLES_PER_FRAME = Channels;
    static constexpr uint32_t BYTES_PER_FRAME = Channels * sizeof(SampleT);

    /// Get buffer capacity in frames
    [[nodiscard]] static constexpr uint32_t capacity() noexcept { return Frames; }

    void reset() {
        write_pos_.store(0, std::memory_order_relaxed);
        read_pos_.store(0, std::memory_order_relaxed);
        read_frac_ = 0;
        playback_started_.store(false, std::memory_order_relaxed);
        underrun_count_ = 0;
        overrun_count_ = 0;
    }

    /// Reset and immediately enable reading with silence prebuffer.
    /// Prebuffer prevents underrun at stream start: USB SOF calls send_audio_in()
    /// before the audio task has written any data to the ring buffer.
    void reset_and_start() {
        reset();
        // Prebuffer 1/4 capacity with silence so send_audio_in() doesn't underrun
        // Using 1/4 instead of 1/2 to leave more room for writes, reducing overrun risk
        constexpr uint32_t prebuffer_frames = Frames / 4;
        __builtin_memset(buffer_, 0, prebuffer_frames * BYTES_PER_FRAME);
        write_pos_.store(prebuffer_frames, std::memory_order_release);
        playback_started_.store(true, std::memory_order_release);
    }

    /// Write frames from USB callback (producer)
    uint32_t write(const SampleT* samples, uint32_t frame_count) {
        uint32_t write = write_pos_.load(std::memory_order_relaxed);
        uint32_t read = read_pos_.load(std::memory_order_acquire);

        uint32_t available = (write - read) & MASK;
        uint32_t free_space = Frames - 1 - available;

        if (frame_count > free_space) {
            frame_count = free_space;
            overrun_count_++;
        }

        if (frame_count > 0) {
            uint32_t write_idx = write & MASK;
            uint32_t first_chunk = Frames - write_idx; // Frames until wrap

            if (frame_count <= first_chunk) {
                // No wrap: single memcpy
                __builtin_memcpy(&buffer_[write_idx * Channels], samples, frame_count * BYTES_PER_FRAME);
            } else {
                // Wrap: two memcpy calls
                __builtin_memcpy(&buffer_[write_idx * Channels], samples, first_chunk * BYTES_PER_FRAME);
                __builtin_memcpy(buffer_,
                                 &samples[static_cast<size_t>(first_chunk) * Channels],
                                 (frame_count - first_chunk) * BYTES_PER_FRAME);
            }
        }

        // Release semantics: ensure all writes to buffer are visible before updating write_pos_
        write_pos_.store((write + frame_count) & MASK, std::memory_order_release);

        return frame_count;
    }

    /// Write frames, overwriting oldest data if buffer is full
    /// This keeps the buffer always full with the most recent data.
    /// Ideal for Audio IN where we want fresh data ready when streaming starts.
    uint32_t write_overwrite(const SampleT* samples, uint32_t frame_count) {
        if (frame_count > Frames - 1) {
            // Can't write more than buffer capacity minus 1
            frame_count = Frames - 1;
        }

        uint32_t write = write_pos_.load(std::memory_order_relaxed);
        uint32_t read = read_pos_.load(std::memory_order_acquire);

        uint32_t available = (write - read) & MASK;
        uint32_t free_space = Frames - 1 - available;

        // If not enough space, advance read pointer to make room
        if (frame_count > free_space) {
            uint32_t need_space = frame_count - free_space;
            read_pos_.store((read + need_space) & MASK, std::memory_order_release);
            overrun_count_++;
        }

        // Now write the data
        uint32_t write_idx = write & MASK;
        uint32_t first_chunk = Frames - write_idx;

        if (frame_count <= first_chunk) {
            __builtin_memcpy(&buffer_[write_idx * Channels], samples, frame_count * BYTES_PER_FRAME);
        } else {
            __builtin_memcpy(&buffer_[write_idx * Channels], samples, first_chunk * BYTES_PER_FRAME);
            __builtin_memcpy(buffer_,
                             &samples[static_cast<size_t>(first_chunk) * Channels],
                             (frame_count - first_chunk) * BYTES_PER_FRAME);
        }

        write_pos_.store((write + frame_count) & MASK, std::memory_order_release);
        return frame_count;
    }

    /// Read frames for Audio DMA (consumer)
    uint32_t read(SampleT* dest, uint32_t frame_count) {
        // Acquire semantics: ensure we see all buffer updates before reading write_pos_
        uint32_t write = write_pos_.load(std::memory_order_acquire);
        uint32_t read = read_pos_.load(std::memory_order_relaxed);
        uint32_t available = (write - read) & MASK;

        // Calculate how many frames we can actually read
        uint32_t to_read = (frame_count < available) ? frame_count : available;

        if (to_read > 0) {
            uint32_t read_idx = read & MASK;
            uint32_t first_chunk = Frames - read_idx; // Frames until wrap

            if (to_read <= first_chunk) {
                __builtin_memcpy(dest, buffer_ + (read_idx * Channels), to_read * BYTES_PER_FRAME);
            } else {
                __builtin_memcpy(dest, buffer_ + (read_idx * Channels), first_chunk * BYTES_PER_FRAME);
                __builtin_memcpy(dest + (first_chunk * Channels), buffer_, (to_read - first_chunk) * BYTES_PER_FRAME);
            }
            // Release semantics: ensure buffer reads complete before updating read_pos_
            read_pos_.store((read + to_read) & MASK, std::memory_order_release);
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
    uint32_t read_interpolated(SampleT* dest, uint32_t frame_count, uint32_t rate_q16) {
        if (!playback_started_.load(std::memory_order_acquire)) {
            __builtin_memset(dest, 0, static_cast<size_t>(frame_count) * BYTES_PER_FRAME);
            return 0;
        }

        // Acquire semantics: ensure we see all buffer updates
        uint32_t write = write_pos_.load(std::memory_order_acquire);
        uint32_t read = read_pos_.load(std::memory_order_relaxed);
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
            uint32_t cur_read = read_pos_.load(std::memory_order_relaxed);
            uint32_t cur_available = (write - cur_read) & MASK;
            if (cur_available < 4) {
                break; // Not enough data
            }

            // Get 4 sample positions for interpolation
            uint32_t idx0 = cur_read & MASK;
            uint32_t idx1 = (cur_read + 1) & MASK;
            uint32_t idx2 = (cur_read + 2) & MASK;
            uint32_t idx3 = (cur_read + 3) & MASK;

            // Interpolate each channel
            for (uint8_t ch = 0; ch < Channels; ++ch) {
                // Read samples - compiler may reorder but DMB at loop start ensures consistency
                SampleT ch_s0 = buffer_[(idx0 * Channels) + ch];
                SampleT ch_s1 = buffer_[(idx1 * Channels) + ch];
                SampleT ch_s2 = buffer_[(idx2 * Channels) + ch];
                SampleT ch_s3 = buffer_[(idx3 * Channels) + ch];

                if constexpr (std::is_same_v<SampleT, int16_t>) {
                    dest[out_frames * Channels + ch] =
                        umidsp::cubic_hermite::interpolate_i16(ch_s0, ch_s1, ch_s2, ch_s3, frac);
                } else {
                    dest[out_frames * Channels + ch] =
                        umidsp::cubic_hermite::interpolate_i32(ch_s0, ch_s1, ch_s2, ch_s3, frac);
                }
            }
            out_frames++;

            // Advance fractional position
            frac += rate_q16;
            uint32_t consumed = frac >> 16;
            frac &= 0xFFFF;

            // Advance read position by consumed whole samples
            read_pos_.store((cur_read + consumed) & MASK, std::memory_order_release);

            // Re-check write position periodically to catch new USB data
            if ((out_frames & 7) == 0) {
                write = write_pos_.load(std::memory_order_acquire);
            }
        }

        read_frac_ = frac;

        // Zero-fill any remaining frames (underrun)
        if (out_frames < frame_count) {
            uint32_t underrun_frames = frame_count - out_frames;
            __builtin_memset(dest + (out_frames * Channels), 0, static_cast<size_t>(underrun_frames) * BYTES_PER_FRAME);
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

    [[nodiscard]] bool is_playback_started() const { return playback_started_.load(std::memory_order_acquire); }
    void start_playback() { playback_started_.store(true, std::memory_order_release); }
    [[nodiscard]] uint32_t buffered_frames() const {
        return (write_pos_.load(std::memory_order_relaxed) - read_pos_.load(std::memory_order_relaxed)) & MASK;
    }
    [[nodiscard]] int32_t buffer_level() const { return static_cast<int32_t>(buffered_frames()); }
    [[nodiscard]] uint32_t underrun_count() const { return underrun_count_; }
    [[nodiscard]] uint32_t overrun_count() const { return overrun_count_; }

    // Debug: get raw sample at buffer index
    [[nodiscard]] SampleT dbg_sample_at(uint32_t idx) const { return buffer_[idx]; }
    [[nodiscard]] uint32_t dbg_write_pos() const { return write_pos_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint32_t dbg_read_pos() const { return read_pos_.load(std::memory_order_relaxed); }

    /// Get consumed input frames from last read_interpolated() call
    /// Use this for USB feedback calculation instead of output frame count
    [[nodiscard]] uint32_t last_consumed_input() const { return last_consumed_input_; }

  private:
    alignas(32) SampleT buffer_[Frames * Channels]{};
    std::atomic<uint32_t> write_pos_{0};
    std::atomic<uint32_t> read_pos_{0};
    uint32_t read_frac_ = 0; // Fractional read position (Q0.16)
    std::atomic<bool> playback_started_{false};
    uint32_t underrun_count_ = 0;
    uint32_t overrun_count_ = 0;
    uint32_t last_consumed_input_ = 0; // Input frames consumed in last read_interpolated()
};

// ============================================================================
// MIDI Processing
// ============================================================================

/// USB MIDI packet processing (shared between UAC1 and UAC2)
/// @deprecated Use UsbMidiClass instead, which integrates with EventRouter.
class MidiProcessor {
  public:
    using MidiCallback = void (*)(uint8_t cable, const uint8_t* data, uint8_t len);
    using SysExCallback = void (*)(const uint8_t* data, uint16_t len);

    MidiCallback on_midi = nullptr;
    SysExCallback on_sysex = nullptr;

    void process_packet(uint8_t header, uint8_t b0, uint8_t b1, uint8_t b2) {
        uint8_t cin = header & 0x0F;
        uint8_t cable = header >> 4;

        switch (cin) {
        case 0x04: // SysEx start/continue
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

        case 0x05: // SysEx ends with 1 byte
            if (sysex_pos_ < sysex_buf_.size()) {
                sysex_buf_[sysex_pos_++] = b0;
            }
            complete_sysex();
            break;

        case 0x06: // SysEx ends with 2 bytes
            if (sysex_pos_ + 2 <= sysex_buf_.size()) {
                sysex_buf_[sysex_pos_++] = b0;
                sysex_buf_[sysex_pos_++] = b1;
            }
            complete_sysex();
            break;

        case 0x07: // SysEx ends with 3 bytes
            if (sysex_pos_ + 3 <= sysex_buf_.size()) {
                sysex_buf_[sysex_pos_++] = b0;
                sysex_buf_[sysex_pos_++] = b1;
                sysex_buf_[sysex_pos_++] = b2;
            }
            complete_sysex();
            break;

        case 0x08: // Note Off
        case 0x09: // Note On
        case 0x0A: // Poly Aftertouch
        case 0x0B: // Control Change
        case 0x0E: // Pitch Bend
            if (on_midi != nullptr) {
                std::array<uint8_t, 3> msg = {b0, b1, b2};
                on_midi(cable, msg.data(), 3);
            }
            break;

        case 0x0C: // Program Change
        case 0x0D: // Channel Aftertouch
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
        case 0x80:
            return 0x08;
        case 0x90:
            return 0x09;
        case 0xA0:
            return 0x0A;
        case 0xB0:
            return 0x0B;
        case 0xC0:
            return 0x0C;
        case 0xD0:
            return 0x0D;
        case 0xE0:
            return 0x0E;
        case 0xF0:
            return 0x04;
        default:
            return 0x0F;
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
inline constexpr uint8_t EFFECT_UNIT = 0x07; // UAC2
inline constexpr uint8_t PROCESSING_UNIT = 0x08;
inline constexpr uint8_t EXTENSION_UNIT = 0x09;
inline constexpr uint8_t CLOCK_SOURCE = 0x0A;          // UAC2
inline constexpr uint8_t CLOCK_SELECTOR = 0x0B;        // UAC2
inline constexpr uint8_t CLOCK_MULTIPLIER = 0x0C;      // UAC2
inline constexpr uint8_t SAMPLE_RATE_CONVERTER = 0x0D; // UAC2
} // namespace ac

// Descriptor subtypes - Audio Streaming
namespace as {
inline constexpr uint8_t GENERAL = 0x01;
inline constexpr uint8_t FORMAT_TYPE = 0x02;
inline constexpr uint8_t ENCODER = 0x03; // UAC2
inline constexpr uint8_t DECODER = 0x04; // UAC2
} // namespace as

// Descriptor subtypes - MIDI Streaming
namespace ms {
inline constexpr uint8_t HEADER = 0x01;
inline constexpr uint8_t MIDI_IN_JACK = 0x02;
inline constexpr uint8_t MIDI_OUT_JACK = 0x03;
inline constexpr uint8_t ELEMENT = 0x04;
inline constexpr uint8_t GENERAL = 0x01; // For endpoint
} // namespace ms

// Terminal types (USB Audio Terminal Types spec)
inline constexpr uint16_t TERMINAL_USB_STREAMING = 0x0101;
inline constexpr uint16_t TERMINAL_SPEAKER = 0x0301;
inline constexpr uint16_t TERMINAL_HEADPHONES = 0x0302;
inline constexpr uint16_t TERMINAL_HEADSET = 0x0402;
inline constexpr uint16_t TERMINAL_MICROPHONE = 0x0201;
inline constexpr uint16_t TERMINAL_LINE_IN = 0x0501;
inline constexpr uint16_t TERMINAL_DIGITAL_IN = 0x0502;
inline constexpr uint16_t TERMINAL_LINE_OUT = 0x0602;
inline constexpr uint16_t TERMINAL_SPDIF_OUT = 0x0605;
inline constexpr uint16_t TERMINAL_SYNTHESIZER = 0x0703;
inline constexpr uint16_t TERMINAL_INSTRUMENT = 0x0710;

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

// Audio function category (UAC2)
inline constexpr uint8_t FUNCTION_CATEGORY_UNDEFINED = 0x00;
inline constexpr uint8_t FUNCTION_CATEGORY_IO_BOX = 0x08;

// Interface protocol
inline constexpr uint8_t IP_VERSION_02_00 = 0x20;
} // namespace uac2

} // namespace uac

// ============================================================================
// MIDI Port Configuration
// ============================================================================

/// MIDI port configuration (for IN or OUT)
template <uint8_t Cables_, uint8_t Endpoint_, uint16_t PacketSize_ = 64>
struct MidiPort {
    static constexpr bool ENABLED = true;
    static constexpr uint8_t CABLES = Cables_;
    static constexpr uint8_t ENDPOINT = Endpoint_;
    static constexpr uint16_t PACKET_SIZE = PacketSize_;
};

/// Disabled MIDI port
struct NoMidiPort {
    static constexpr bool ENABLED = false;
    static constexpr uint8_t CABLES = 0;
    static constexpr uint8_t ENDPOINT = 0;
    static constexpr uint16_t PACKET_SIZE = 0;
};

} // namespace umiusb
