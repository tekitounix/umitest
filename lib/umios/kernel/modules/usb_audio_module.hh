// SPDX-License-Identifier: MIT
// umios USB Audio Module - Kernel module for USB Audio streaming
//
// Key design: USB operations happen ONLY in USB task context
// SysTick/Timers only set flags, never call USB functions directly

#pragma once

#include <cstdint>

namespace umi::modules {

/// USB Audio state machine
enum class UsbAudioState {
    Disconnected,   // USB not connected
    Connected,      // USB connected, not streaming
    Streaming,      // Audio streaming active
    Suspended,      // USB suspended (low power)
};

/// USB Audio module configuration
struct UsbAudioConfig {
    uint32_t sample_rate = 48000;
    uint32_t frame_size = 48;       // Samples per USB frame (1ms)
    uint32_t channels = 2;          // Stereo
    bool enable_audio_in = true;    // Microphone/synth to host
    bool enable_audio_out = true;   // Host to speakers
    bool enable_midi = true;        // MIDI interface
};

/// USB Audio events (for task notification)
namespace UsbAudioEvent {
    constexpr uint32_t Irq          = 1 << 0;  // USB IRQ occurred
    constexpr uint32_t SofTick      = 1 << 1;  // SOF tick (1ms timer)
    constexpr uint32_t StreamStart  = 1 << 2;  // Host started streaming
    constexpr uint32_t StreamStop   = 1 << 3;  // Host stopped streaming
    constexpr uint32_t Suspend      = 1 << 4;  // USB suspended
    constexpr uint32_t Resume       = 1 << 5;  // USB resumed
}

/// USB Audio task interface
/// All USB operations must be called from within this task
struct IUsbAudioTask {
    virtual ~IUsbAudioTask() = default;
    
    /// Main task loop - processes USB events
    virtual void run() = 0;
    
    /// Get current state
    virtual UsbAudioState state() const = 0;
    
    /// Check if audio IN is streaming
    virtual bool is_streaming_in() const = 0;
    
    /// Check if audio OUT is streaming  
    virtual bool is_streaming_out() const = 0;
};

/// Timer callback interface for SOF emulation
/// When USB is connected, actual SOF drives timing
/// When disconnected, kernel timer provides fallback
struct ISofCallback {
    virtual ~ISofCallback() = default;
    
    /// Called at 1ms intervals (from task context, not ISR!)
    virtual void on_sof_tick() = 0;
};

}  // namespace umi::modules
