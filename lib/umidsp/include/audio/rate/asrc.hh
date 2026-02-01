// SPDX-License-Identifier: MIT
// UMI-DSP: ASRC (Asynchronous Sample Rate Conversion)
//
// High-level ASRC processor combining PI control and interpolation.
// For primitive building blocks, see core/*.hh
#pragma once

#include <cstdint>
#include <cstring>

#include "../../core/interpolate.hh"
#include "../../core/phase.hh"
#include "pi_controller.hh"

namespace umidsp {

// ============================================================================
// ASRC Quality (alias for InterpolateQuality)
// ============================================================================

using AsrcQuality = InterpolateQuality;

// ============================================================================
// ASRC Processor (Runtime-configurable)
// ============================================================================

/// Complete ASRC processor with runtime configuration
/// Combines PI rate control with interpolation for clock drift compensation.
///
/// Template parameters:
///   Channels: number of audio channels (1=mono, 2=stereo)
///   BufferFrames: size of the source buffer (must be power of 2)
template<uint8_t Channels = 2, uint32_t BufferFrames = 256>
class AsrcProcessor {
public:
    static_assert((BufferFrames & (BufferFrames - 1)) == 0, "BufferFrames must be power of 2");
    static constexpr uint32_t MASK = BufferFrames - 1;

    AsrcProcessor() = default;
    
    explicit AsrcProcessor(const PiConfig& cfg, AsrcQuality quality = AsrcQuality::CUBIC_HERMITE)
        : pi_controller_(cfg), quality_(quality) {}

    void set_config(const PiConfig& cfg) { pi_controller_.set_config(cfg); }
    const PiConfig& config() const { return pi_controller_.config(); }
    
    void set_quality(AsrcQuality q) { quality_ = q; }
    AsrcQuality quality() const { return quality_; }

    void reset() {
        pi_controller_.reset();
        read_frac_ = 0;
        frames_in_ = 0;
        frames_out_ = 0;
    }

    /// Process ASRC: read from circular buffer with rate conversion
    /// src_buffer: circular buffer of frames (Channels samples per frame)
    /// read_pos: current read position in buffer (updated by caller)
    /// write_pos: current write position in buffer
    /// dest: output buffer
    /// frame_count: number of output frames to generate
    /// Returns: number of input frames consumed
    uint32_t process(const int16_t* src_buffer,
                     uint32_t& read_pos,
                     uint32_t write_pos,
                     int16_t* dest,
                     uint32_t frame_count) {
        // Calculate buffer level and update PI controller
        uint32_t available = (write_pos - read_pos) & MASK;
        int32_t ppm = pi_controller_.update(static_cast<int32_t>(available));
        uint32_t rate = PiRateController::ppm_to_rate_q16(ppm);

        // Need at least 4 frames for 4-point interpolation
        const uint32_t min_frames = (quality_ == AsrcQuality::LINEAR) ? 2 : 4;
        if (available < min_frames) {
            __builtin_memset(dest, 0, frame_count * Channels * sizeof(int16_t));
            return 0;
        }

        uint32_t out_frames = 0;
        uint32_t frac = read_frac_;
        uint32_t consumed = 0;

        while (out_frames < frame_count) {
            uint32_t cur_available = (write_pos - read_pos) & MASK;
            if (cur_available < min_frames) break;

            // Get 4 frame indices for interpolation
            uint32_t idx0 = read_pos & MASK;
            uint32_t idx1 = (read_pos + 1) & MASK;
            uint32_t idx2 = (read_pos + 2) & MASK;
            uint32_t idx3 = (read_pos + 3) & MASK;

            // Interpolate each channel
            for (uint8_t ch = 0; ch < Channels; ++ch) {
                int16_t s0 = src_buffer[idx0 * Channels + ch];
                int16_t s1 = src_buffer[idx1 * Channels + ch];
                int16_t s2 = src_buffer[idx2 * Channels + ch];
                int16_t s3 = src_buffer[idx3 * Channels + ch];

                dest[out_frames * Channels + ch] =
                    interpolate::dispatch_i16(quality_, s0, s1, s2, s3, frac);
            }
            out_frames++;

            // Advance fractional position
            frac += rate;
            uint32_t step = frac >> 16;
            frac &= 0xFFFF;

            read_pos = (read_pos + step) & MASK;
            consumed += step;
        }

        read_frac_ = frac;
        frames_in_ += consumed;
        frames_out_ += out_frames;

        // Zero-fill remaining frames
        if (out_frames < frame_count) {
            uint32_t remaining = frame_count - out_frames;
            __builtin_memset(dest + out_frames * Channels, 0,
                            remaining * Channels * sizeof(int16_t));
        }

        return consumed;
    }

    // Monitoring API
    [[nodiscard]] int32_t current_ppm() const { return pi_controller_.current_ppm(); }
    [[nodiscard]] uint32_t current_rate_q16() const {
        return PiRateController::ppm_to_rate_q16(pi_controller_.current_ppm());
    }
    [[nodiscard]] uint64_t frames_in() const { return frames_in_; }
    [[nodiscard]] uint64_t frames_out() const { return frames_out_; }
    
    /// Effective sample rate ratio = frames_in / frames_out
    [[nodiscard]] float effective_ratio() const {
        return frames_out_ > 0 ? static_cast<float>(frames_in_) / frames_out_ : 1.0f;
    }

    /// Direct access to PI controller
    PiRateController& pi_controller() { return pi_controller_; }
    const PiRateController& pi_controller() const { return pi_controller_; }

private:
    PiRateController pi_controller_{PiConfig::default_config()};
    AsrcQuality quality_ = AsrcQuality::CUBIC_HERMITE;
    uint32_t read_frac_ = 0;
    uint64_t frames_in_ = 0;
    uint64_t frames_out_ = 0;
};

// ============================================================================
// Legacy Template-Based ASRC Processor (for backward compatibility)
// ============================================================================

template<uint8_t Channels = 2, uint32_t BufferFrames = 256>
class AsrcProcessorT {
public:
    static_assert((BufferFrames & (BufferFrames - 1)) == 0, "BufferFrames must be power of 2");
    static constexpr uint32_t MASK = BufferFrames - 1;

    void reset() {
        pi_controller_.reset();
        read_frac_ = 0;
    }

    uint32_t process(const int16_t* src_buffer,
                     uint32_t& read_pos,
                     uint32_t write_pos,
                     int16_t* dest,
                     uint32_t frame_count) {
        uint32_t available = (write_pos - read_pos) & MASK;
        int32_t ppm = pi_controller_.update(static_cast<int32_t>(available));
        uint32_t rate = UsbAudioPiController::ppm_to_rate_q16(ppm);

        if (available < 4) {
            __builtin_memset(dest, 0, frame_count * Channels * sizeof(int16_t));
            return 0;
        }

        uint32_t out_frames = 0;
        uint32_t frac = read_frac_;
        uint32_t consumed = 0;

        while (out_frames < frame_count) {
            uint32_t cur_available = (write_pos - read_pos) & MASK;
            if (cur_available < 4) break;

            uint32_t idx0 = read_pos & MASK;
            uint32_t idx1 = (read_pos + 1) & MASK;
            uint32_t idx2 = (read_pos + 2) & MASK;
            uint32_t idx3 = (read_pos + 3) & MASK;

            for (uint8_t ch = 0; ch < Channels; ++ch) {
                int16_t s0 = src_buffer[idx0 * Channels + ch];
                int16_t s1 = src_buffer[idx1 * Channels + ch];
                int16_t s2 = src_buffer[idx2 * Channels + ch];
                int16_t s3 = src_buffer[idx3 * Channels + ch];

                dest[out_frames * Channels + ch] =
                    cubic_hermite::interpolate_i16(s0, s1, s2, s3, frac);
            }
            out_frames++;

            frac += rate;
            uint32_t step = frac >> 16;
            frac &= 0xFFFF;

            read_pos = (read_pos + step) & MASK;
            consumed += step;
        }

        read_frac_ = frac;

        if (out_frames < frame_count) {
            uint32_t remaining = frame_count - out_frames;
            __builtin_memset(dest + out_frames * Channels, 0,
                            remaining * Channels * sizeof(int16_t));
        }

        return consumed;
    }

    [[nodiscard]] int32_t current_ppm() const { return pi_controller_.current_ppm(); }
    [[nodiscard]] uint32_t current_rate_q16() const {
        return UsbAudioPiController::ppm_to_rate_q16(pi_controller_.current_ppm());
    }

    UsbAudioPiController& pi_controller() { return pi_controller_; }
    const UsbAudioPiController& pi_controller() const { return pi_controller_; }

private:
    UsbAudioPiController pi_controller_;
    uint32_t read_frac_ = 0;
};

}  // namespace umidsp
