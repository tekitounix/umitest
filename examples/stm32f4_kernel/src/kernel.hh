// SPDX-License-Identifier: MIT
// STM32F4 Kernel Interface
// RTOS, Syscall handling, Audio processing

#pragma once

#include <cstdint>

namespace umi::kernel {

// Callbacks from port layer (ISR context)
void on_audio_buffer_ready(uint16_t* buf);
void on_pdm_buffer_ready(uint16_t* buf);

// Tick counter (microseconds)
extern volatile uint32_t g_tick_us;

// Current sample rate
extern volatile uint32_t g_current_sample_rate;

// Initialization API (called from main.cc)
void setup_usb_callbacks();
void init_shared_memory();
void init_loader(uint8_t* app_ram_start, uintptr_t app_ram_size);
void* load_app(const uint8_t* app_image_start);
void start_rtos(void* app_entry);

} // namespace umi::kernel
