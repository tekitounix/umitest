// SPDX-License-Identifier: MIT
// umidi - Hardware timestamp to sample position conversion (05-midi.md)
#pragma once

#include <algorithm>
#include <cstdint>

namespace umidi {

/// Convert a hardware timestamp (μs) to a buffer-relative sample position.
/// Used by EventRouter to place MIDI events at sample-accurate positions
/// within the audio processing buffer.
///
/// @param event_time_us  Event receive time (μs, monotonic)
/// @param block_start_us Audio block start time (μs, monotonic)
/// @param sample_rate    Sample rate in Hz (e.g. 48000)
/// @param buffer_size    Audio buffer size in samples (e.g. 256)
/// @return Sample position within buffer [0, buffer_size-1]
[[nodiscard]] constexpr uint16_t hw_timestamp_to_sample_pos(
    uint64_t event_time_us, uint64_t block_start_us,
    uint32_t sample_rate, uint32_t buffer_size) noexcept
{
    // Events before block start get placed at sample 0
    if (event_time_us <= block_start_us || buffer_size == 0) {
        return 0;
    }

    uint64_t elapsed_us = event_time_us - block_start_us;
    uint32_t sample_offset = static_cast<uint32_t>(elapsed_us * sample_rate / 1'000'000);
    return static_cast<uint16_t>(std::min(sample_offset, buffer_size - 1));
}

} // namespace umidi
