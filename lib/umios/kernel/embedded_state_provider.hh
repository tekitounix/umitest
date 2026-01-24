// SPDX-License-Identifier: MIT
/// @file embedded_state_provider.hh
/// @brief State provider for embedded platforms (STM32, etc.)
///
/// Implements the StateProvider interface required by ShellCommands.
/// Bridges between kernel globals and the unified shell system.

#pragma once

#include "shell_commands.hh"
#include <cstdint>

namespace umi::os {

/// State provider for embedded platforms
/// Template parameter allows customization per-platform
template<typename Platform>
class EmbeddedStateProvider {
public:
    EmbeddedStateProvider() = default;

    /// Get kernel state snapshot
    KernelStateView& state() {
        // Update from platform-specific sources
        cached_state_.uptime_us = Platform::get_uptime_us();
        cached_state_.audio_buffer_count = Platform::get_audio_buffer_count();
        cached_state_.audio_drop_count = Platform::get_audio_drop_count();
        cached_state_.dsp_load_percent_x100 = Platform::get_dsp_load_x100();
        cached_state_.audio_running = Platform::is_audio_running();
        cached_state_.midi_rx_count = Platform::get_midi_rx_count();
        cached_state_.midi_tx_count = Platform::get_midi_tx_count();
        cached_state_.context_switches = Platform::get_context_switches();
        cached_state_.task_count = Platform::get_task_count();
        cached_state_.log_level = 2;
        return cached_state_;
    }

    /// Get shell config
    ShellConfig& config() {
        return config_;
    }

    /// Get error log
    ErrorLog<16>& error_log() {
        return error_log_;
    }

    /// Get system mode
    SystemMode& system_mode() {
        return system_mode_;
    }

    /// Reset system
    void reset_system() {
        Platform::system_reset();
    }

    /// Feed watchdog
    void feed_watchdog() {
        Platform::feed_watchdog();
    }

    /// Enable/disable watchdog
    void enable_watchdog(bool enable) {
        Platform::enable_watchdog(enable);
    }

private:
    KernelStateView cached_state_;
    ShellConfig config_;
    ErrorLog<16> error_log_;
    SystemMode system_mode_ = SystemMode::Normal;
};

} // namespace umi::os
