// SPDX-License-Identifier: MIT
// UMI-USB: Portable USB Device Stack
//
// A modern C++23 USB device stack with:
// - Hardware abstraction via Concepts
// - Compile-time descriptor generation
// - Zero-overhead abstractions
//
// Usage:
//   #include <umiusb.hh>
//   #include <hal/stm32_otg.hh>  // or other HAL backend
//
//   umiusb::Stm32FsHal hal;
//   umiusb::AudioMidi<1> audio;  // MIDI on EP1
//   umiusb::Device device(hal, audio, {.vendor_id = 0x1209, .product_id = 0x0001});
//
//   device.init();
//   hal.connect();
//
//   while (true) {
//       device.poll();
//   }
//
#pragma once

// Core types and descriptors
#include "types.hh"

// Hardware abstraction concept
#include "hal.hh"

// Device core
#include "device.hh"

// USB Classes
#include "audio_types.hh"
#include "audio_interface.hh"

// Compile-time descriptor builder
#include "descriptor.hh"

// Version
namespace umiusb {
inline constexpr uint16_t VERSION_MAJOR = 0;
inline constexpr uint16_t VERSION_MINOR = 1;
inline constexpr uint16_t VERSION_PATCH = 0;
}
