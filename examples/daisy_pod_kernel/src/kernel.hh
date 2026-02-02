// SPDX-License-Identifier: MIT
// Daisy Pod Kernel - Public API
#pragma once

#include <cstdint>

namespace daisy_kernel {

/// Initialize kernel state (debug vars, etc.)
void init();

/// Initialize USB Audio + MIDI
void init_usb();

/// Initialize Pod HID controls (knobs, buttons, LEDs, encoder)
void init_hid(float update_rate);

/// Called from DMA ISR when audio buffer half/full transfer complete
void on_audio_buffer_ready(std::int32_t* tx, std::int32_t* rx);

/// Called from USART1 IRQ for MIDI UART receive
void handle_usart1_irq();

/// Start RTOS scheduler (does not return)
[[noreturn]] void start_rtos();

} // namespace daisy_kernel
