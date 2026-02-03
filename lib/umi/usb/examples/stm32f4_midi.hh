// SPDX-License-Identifier: MIT
// Example: USB MIDI Device using umiusb on STM32F4
//
// This shows how to use the umiusb library for USB MIDI.
// Include this file and call the setup functions.
//
#pragma once

#include <umiusb.hh>
#include <hal/stm32_otg.hh>
#include <array>

namespace umiusb::example {

// ============================================================================
// String Descriptors
// ============================================================================

// Build string descriptors at compile time
inline constexpr auto str_manufacturer = StringDesc("UMI-OS");
inline constexpr auto str_product = StringDesc("UMI Synth");

// String descriptor spans for Device class
inline constexpr std::array<std::span<const uint8_t>, 2> strings = {
    std::span<const uint8_t>(str_manufacturer.data(), str_manufacturer.size()),
    std::span<const uint8_t>(str_product.data(), str_product.size()),
};

// ============================================================================
// Device Configuration
// ============================================================================

// HAL backend
inline Stm32FsHal hal;

// Audio class with MIDI
inline AudioMidi<1, 64> audio;

// Device info
inline constexpr DeviceInfo device_info = {
    .vendor_id = 0x1209,       // pid.codes test VID
    .product_id = 0x0001,
    .device_version = 0x0200,  // v2.00
    .manufacturer_idx = 1,
    .product_idx = 2,
    .serial_idx = 0,
};

// Device instance
inline Device<Stm32FsHal, AudioMidi<1, 64>> device(hal, audio, device_info);

// ============================================================================
// Setup Functions
// ============================================================================

/// Initialize USB MIDI device
inline void init_usb_midi() {
    device.set_strings(strings);
    device.init();
    hal.connect();
}

/// Poll USB (call from main loop or IRQ)
inline void poll_usb_midi() {
    device.poll();
}

/// Set MIDI receive callback
inline void set_midi_callback(AudioMidi<1, 64>::MidiCallback callback) {
    audio.on_midi = callback;
}

/// Set SysEx receive callback
inline void set_sysex_callback(AudioMidi<1, 64>::SysExCallback callback) {
    audio.on_sysex = callback;
}

/// Send MIDI message
inline void send_note_on(uint8_t ch, uint8_t note, uint8_t vel) {
    audio.send_note_on(hal, ch, note, vel);
}

inline void send_note_off(uint8_t ch, uint8_t note, uint8_t vel = 0) {
    audio.send_note_off(hal, ch, note, vel);
}

inline void send_cc(uint8_t ch, uint8_t cc, uint8_t val) {
    audio.send_cc(hal, ch, cc, val);
}

/// Check if device is configured
inline bool is_configured() {
    return device.is_configured();
}

}  // namespace umiusb::example
