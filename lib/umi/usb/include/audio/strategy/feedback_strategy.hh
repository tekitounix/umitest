// SPDX-License-Identifier: MIT
// UMI-USB: Feedback Strategy
#pragma once

#include <array>
#include <cstdint>
#include <concepts>
#include "audio/audio_types.hh"

namespace umiusb {

// ============================================================================
// FeedbackStrategy Concept
// ============================================================================

template<typename T>
concept FeedbackStrategy = requires(T& f, const T& cf) {
    { f.reset(uint32_t{}) } -> std::same_as<void>;
    { f.set_buffer_half_size(uint32_t{}) } -> std::same_as<void>;
    { f.set_actual_rate(uint32_t{}) } -> std::same_as<void>;
    { f.update_from_buffer_level(int32_t{}) } -> std::same_as<void>;
    { cf.get_feedback() } -> std::convertible_to<uint32_t>;
    { cf.get_feedback_bytes() } -> std::convertible_to<std::array<uint8_t, 3>>;
};

// ============================================================================
// Default Feedback Strategy
// ============================================================================

/// Default implementation wrapping the existing FeedbackCalculator.
/// Realtime-safe: pure arithmetic.
template<UacVersion Version = UacVersion::UAC1>
struct DefaultFeedbackStrategy {
    void reset(uint32_t nominal_rate) { calc_.reset(nominal_rate); }
    void set_buffer_half_size(uint32_t half_size) { calc_.set_buffer_half_size(half_size); }
    void set_actual_rate(uint32_t rate) { calc_.set_actual_rate(rate); }
    void update_from_buffer_level(int32_t writable) { calc_.update_from_buffer_level(writable); }
    [[nodiscard]] uint32_t get_feedback() const { return calc_.get_feedback(); }
    [[nodiscard]] std::array<uint8_t, 3> get_feedback_bytes() const { return calc_.get_feedback_bytes(); }

private:
    FeedbackCalculator<Version> calc_;
};

static_assert(FeedbackStrategy<DefaultFeedbackStrategy<UacVersion::UAC1>>);
static_assert(FeedbackStrategy<DefaultFeedbackStrategy<UacVersion::UAC2>>);

}  // namespace umiusb
