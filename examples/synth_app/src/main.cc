// SPDX-License-Identifier: MIT
// Polyphonic Synth Application (.umiapp)
// Uses PolySynth from headless_webhost
// Receives MIDI via kernel syscall, outputs audio via synth_process callback

#include <umi_app.hh>
#include <synth.hh>  // headless_webhost/src/synth.hh

// MIDI syscall numbers (must match kernel)
namespace midi_syscall {
    inline constexpr uint32_t MidiRecv = 51;
}

// MIDI message structure (must match kernel)
struct MidiMsg {
    uint8_t data[4];
    uint8_t len;
};

// Receive MIDI from kernel (returns len, 0 if none)
static int midi_recv(MidiMsg* msg) {
    return umi::syscall::call(midi_syscall::MidiRecv, 
                              reinterpret_cast<uint32_t>(msg));
}

// ============================================================================
// Synth Instance
// ============================================================================

static umi::synth::PolySynth g_synth;
static bool g_initialized = false;

// ============================================================================
// Audio Process Callback (called from kernel audio ISR)
// ============================================================================

extern "C" void synth_process(float* output, const float* /*input*/, 
                              uint32_t frames, float dt) {
    // Initialize synth on first call (dt = 1/sample_rate)
    if (!g_initialized) {
        float sample_rate = 1.0f / dt;
        g_synth.init(sample_rate);
        g_initialized = true;
    }
    
    // Process all pending MIDI messages
    MidiMsg msg;
    while (midi_recv(&msg) > 0) {
        g_synth.handle_midi(msg.data, msg.len);
    }
    
    // Generate stereo audio output
    for (uint32_t i = 0; i < frames; ++i) {
        float sample = g_synth.process_sample();
        output[i * 2 + 0] = sample;  // Left
        output[i * 2 + 1] = sample;  // Right
    }
}

// ============================================================================
// Main
// ============================================================================

// Debug: toggle GPIO (if accessible from unprivileged mode...)
// Actually we're running in privileged mode when kernel calls us
namespace {
struct GPIORegs {
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t LCKR;
    volatile uint32_t AFRL;
    volatile uint32_t AFRH;
};
static auto& debug_gpio = *reinterpret_cast<GPIORegs*>(0x40020C00);  // GPIOD
}

int main() {
    // Debug: we're called by kernel in privileged mode
    // Orange LED (PD13) shows we entered main
    debug_gpio.BSRR = (1 << 13);  // Set PD13 (orange LED)
    
    // Register process function with kernel
    umi::register_processor(synth_process);
    
    // If we get here, syscall returned
    // Blue LED + orange = both on = syscall completed
    debug_gpio.BSRR = (1 << 15);  // Set PD15 (blue LED)
    
    // Return to kernel - audio processing happens via callback
    return 0;
}
