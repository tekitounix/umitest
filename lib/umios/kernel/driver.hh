// SPDX-License-Identifier: MIT
// UMI-OS Driver Interface
#pragma once

#include <cstdint>

namespace umi::driver {

// ============================================================================
// Driver Categories
// ============================================================================

enum class Category : uint8_t {
    AUDIO = 0,    // Audio I/O (codec, I2S, etc.)
    MIDI = 1,     // MIDI I/O
    TIMER = 2,    // System timer
    UART = 3,     // Serial communication
    STORAGE = 4,  // Flash/SD card
    DISPLAY = 5,  // LCD/OLED
    INPUT = 6,    // Buttons, encoders
    CUSTOM = 255, // Application-defined
};

// ============================================================================
// Driver Operations Interface
// ============================================================================

/// Driver operations table
/// All drivers implement this interface
struct Ops {
    const char* name;
    Category category;

    /// Initialize driver with optional configuration
    /// @param config  Driver-specific configuration (may be nullptr)
    /// @return 0 on success, negative error code on failure
    int (*init)(const void* config);

    /// Deinitialize driver
    void (*deinit)();

    /// Handle interrupt
    /// @param irq_num  IRQ number that triggered
    void (*irq)(uint32_t irq_num);
};

// ============================================================================
// Driver Registration
// ============================================================================

/// Register a driver in the .drivers section
/// The kernel iterates this section to find and initialize drivers
#define UMI_REGISTER_DRIVER(drv_name, ops_ptr)                                  \
    [[gnu::section(".drivers"), gnu::used]] static const ::umi::driver::Ops* const \
        _drv_reg_##drv_name = ops_ptr

/// Driver iterator (used by kernel during init)
extern const Ops* __drivers_start[];
extern const Ops* __drivers_end[];

inline void init_all_drivers() {
    for (const Ops** p = __drivers_start; p < __drivers_end; ++p) {
        if (*p && (*p)->init) {
            (*p)->init(nullptr);
        }
    }
}

// ============================================================================
// Common Driver Configurations
// ============================================================================

struct TimerConfig {
    uint32_t tick_hz;  // Tick frequency (e.g., 1000 for 1ms)
};

struct UartConfig {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    uint8_t parity;  // 0=none, 1=odd, 2=even
};

struct AudioConfig {
    uint32_t sample_rate;
    uint16_t buffer_size;
    uint8_t channels;
    uint8_t bit_depth;
};

}  // namespace umi::driver
