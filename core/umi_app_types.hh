#pragma once
// =====================================================================
// UMI Application Types
// =====================================================================
//
// Common type definitions shared by all platform implementations.
// No platform-specific code here.
//
// =====================================================================

#include <cstdint>
#include <cstddef>

namespace umi::app {

// =====================================================================
// Shared Memory Region IDs
// =====================================================================

enum class RegionId : std::uint8_t {
    Audio       = 0,
    Midi        = 1,
    Framebuffer = 2,
    HwState     = 3,
};

// =====================================================================
// Shared Memory Descriptor
// =====================================================================

struct RegionDesc {
    void* base {nullptr};
    std::size_t size {0};
    
    constexpr bool valid() const { return base != nullptr && size > 0; }
    
    template <typename T>
    T* as() { return static_cast<T*>(base); }
    
    template <typename T>
    const T* as() const { return static_cast<const T*>(base); }
};

// =====================================================================
// Event Bits
// =====================================================================

namespace event {
    constexpr std::uint32_t AudioReady = 1 << 0;
    constexpr std::uint32_t MidiReady  = 1 << 1;
    constexpr std::uint32_t VSync      = 1 << 2;
}

// =====================================================================
// Common Shared Memory Layouts
// =====================================================================

/// Audio buffer layout in shared memory
struct AudioBuffer {
    float* input;
    float* output;
    std::size_t frames;
    std::uint8_t channels;
    
    std::size_t sample_count() const { return frames * channels; }
};

/// Hardware state (kernel updates, app reads)
struct HwState {
    volatile std::uint64_t uptime_usec;
    volatile std::uint32_t sample_rate;
    volatile std::uint16_t adc_values[8];
    volatile std::uint32_t gpio_state;
    volatile std::uint8_t encoder_positions[4];
    volatile std::uint8_t cpu_load_percent;
    volatile std::uint8_t temperature_c;
};

/// MIDI ring buffer header
struct MidiRing {
    volatile std::size_t head;
    volatile std::size_t tail;
    std::size_t capacity;
    std::uint8_t* data;
    
    bool empty() const { return head == tail; }
    std::size_t available() const { return (head - tail) % capacity; }
};

/// Framebuffer descriptor
struct Framebuffer {
    std::uint16_t* pixels;  // RGB565
    std::uint16_t width;
    std::uint16_t height;
    std::uint16_t stride;
    volatile std::uint8_t* vsync_flag;
};

} // namespace umi::app
