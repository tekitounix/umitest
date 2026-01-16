// SPDX-License-Identifier: MIT
// UMI-OS - Universal Musical Instruments Operating System
// Core type definitions

#pragma once

#include <cstdint>
#include <cstddef>

namespace umi {

/// Audio sample type
using sample_t = float;

/// Sample position in the stream (absolute)
using sample_position_t = uint64_t;

/// Port identifier
using port_id_t = uint32_t;

/// Parameter identifier  
using param_id_t = uint32_t;

/// Maximum channels per port
inline constexpr size_t MAX_CHANNELS = 8;

/// Maximum events per buffer
inline constexpr size_t MAX_EVENTS_PER_BUFFER = 256;

/// Maximum parameters per processor
inline constexpr size_t MAX_PARAMS = 128;

/// Default sample rate (Hz)
inline constexpr uint32_t kDefaultSampleRate = 48000;

/// Default block size (samples)
inline constexpr uint32_t kDefaultBlockSize = 64;

} // namespace umi
