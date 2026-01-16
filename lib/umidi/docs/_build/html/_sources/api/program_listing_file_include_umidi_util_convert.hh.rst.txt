
.. _program_listing_file_include_umidi_util_convert.hh:

Program Listing for File convert.hh
===================================

|exhale_lsh| :ref:`Return to documentation for file <file_include_umidi_util_convert.hh>` (``include/umidi/util/convert.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   // UMI-OS MIDI Library - Value Conversion Utilities
   // MIDI 1.0 <-> MIDI 2.0 value conversions
   #pragma once
   
   #include "../core/ump.hh"
   #include <cstdint>
   
   namespace umidi::convert {
   
   // =============================================================================
   // Velocity Conversion (MIDI 2.0 Section D.2.1 compliant)
   // =============================================================================
   
   [[nodiscard]] constexpr uint8_t velocity_16_to_7(uint16_t velocity_16) noexcept {
       if (velocity_16 == 0) return 0;
   
       uint8_t velocity_7 = velocity_16 >> 9;
   
       // Prevent 0 becoming Note Off in MIDI 1.0
       if (velocity_7 == 0 && velocity_16 > 0) {
           velocity_7 = 1;
       }
   
       return velocity_7;
   }
   
   [[nodiscard]] constexpr uint16_t velocity_7_to_16(uint8_t velocity_7) noexcept {
       if (velocity_7 == 0) return 0;
   
       // Scale 7bit to 16bit with interpolation for better precision
       return (uint16_t(velocity_7) << 9) |
              (uint16_t(velocity_7) << 2) |
              (velocity_7 >> 5);
   }
   
   // =============================================================================
   // Controller Value Conversion
   // =============================================================================
   
   [[nodiscard]] constexpr uint16_t cc_7_to_14(uint8_t value_7) noexcept {
       return (uint16_t(value_7) << 7) | value_7;
   }
   
   [[nodiscard]] constexpr uint8_t cc_14_to_7(uint16_t value_14) noexcept {
       return uint8_t((value_14 >> 7) & 0x7F);
   }
   
   [[nodiscard]] constexpr uint32_t cc_7_to_32(uint8_t value_7) noexcept {
       uint32_t scaled = uint32_t(value_7) << 25;
       scaled |= uint32_t(value_7) << 18;
       scaled |= uint32_t(value_7) << 11;
       scaled |= uint32_t(value_7) << 4;
       scaled |= value_7 >> 3;
       return scaled;
   }
   
   [[nodiscard]] constexpr uint8_t cc_32_to_7(uint32_t value_32) noexcept {
       return uint8_t((value_32 >> 25) & 0x7F);
   }
   
   [[nodiscard]] constexpr uint32_t cc_14_to_32(uint16_t value_14) noexcept {
       uint32_t scaled = uint32_t(value_14) << 18;
       scaled |= uint32_t(value_14) << 4;
       scaled |= value_14 >> 10;
       return scaled;
   }
   
   [[nodiscard]] constexpr uint16_t cc_32_to_14(uint32_t value_32) noexcept {
       return uint16_t((value_32 >> 18) & 0x3FFF);
   }
   
   // =============================================================================
   // Pitch Bend Conversion
   // =============================================================================
   
   [[nodiscard]] constexpr uint32_t pitch_bend_14_to_32(uint16_t bend_14) noexcept {
       uint32_t scaled = uint32_t(bend_14) << 18;
       scaled |= uint32_t(bend_14) << 4;
       scaled |= bend_14 >> 10;
       return scaled;
   }
   
   [[nodiscard]] constexpr uint16_t pitch_bend_32_to_14(uint32_t bend_32) noexcept {
       return uint16_t((bend_32 >> 18) & 0x3FFF);
   }
   
   [[nodiscard]] constexpr float pitch_bend_to_semitones(uint16_t bend_value, float range = 2.0f) noexcept {
       return (float(bend_value) - 8192.0f) / 8192.0f * range;
   }
   
   [[nodiscard]] constexpr uint16_t semitones_to_pitch_bend(float semitones, float range = 2.0f) noexcept {
       float normalized = semitones / range;
       if (normalized < -1.0f) normalized = -1.0f;
       if (normalized > 1.0f) normalized = 1.0f;
       float value = (normalized + 1.0f) * 8191.5f;
       return uint16_t(value + 0.5f);
   }
   
   // =============================================================================
   // Channel Pressure Conversion
   // =============================================================================
   
   [[nodiscard]] constexpr uint32_t pressure_7_to_32(uint8_t pressure_7) noexcept {
       return cc_7_to_32(pressure_7);
   }
   
   [[nodiscard]] constexpr uint8_t pressure_32_to_7(uint32_t pressure_32) noexcept {
       return cc_32_to_7(pressure_32);
   }
   
   // =============================================================================
   // Note On Velocity=0 Handling (MIDI 1.0 specific)
   // =============================================================================
   
   [[nodiscard]] constexpr bool is_note_off_equivalent(const UMP32& ump) noexcept {
       return (ump.word & 0xF0F00000u) == 0x20900000u && (ump.word & 0x7Fu) == 0;
   }
   
   [[nodiscard]] constexpr UMP32 convert_to_note_off(const UMP32& note_on) noexcept {
       // Change status from 0x9X to 0x8X, set default release velocity
       uint32_t w = note_on.word;
       w = (w & 0xFF0FFF00u) | 0x00800000u | 64u;  // Note Off + release vel=64
       return UMP32(w);
   }
   
   // =============================================================================
   // Frequency Conversion
   // =============================================================================
   
   inline float note_to_frequency(uint8_t note, float a4_freq = 440.0f) noexcept {
       // Standard formula: f = 440 * 2^((n-69)/12)
       float exponent = (float(note) - 69.0f) / 12.0f;
       // Manual power of 2 approximation for embedded
       float x = exponent * 0.6931471805599453f;  // ln(2)
       float result = 1.0f;
       float term = 1.0f;
       for (int i = 1; i < 8; ++i) {
           term *= x / float(i);
           result += term;
       }
       return a4_freq * result;
   }
   
   inline uint8_t frequency_to_note(float freq, float a4_freq = 440.0f) noexcept {
       // n = 69 + 12 * log2(f/440)
       float ratio = freq / a4_freq;
       // Manual log2 approximation
       float log2_ratio = 0.0f;
       while (ratio >= 2.0f) { ratio *= 0.5f; log2_ratio += 1.0f; }
       while (ratio < 1.0f) { ratio *= 2.0f; log2_ratio -= 1.0f; }
       // Taylor series for log2 around 1
       float x = ratio - 1.0f;
       log2_ratio += x * (1.0f - x * (0.5f - x / 3.0f)) / 0.6931471805599453f;
   
       float note_float = 69.0f + 12.0f * log2_ratio;
       return uint8_t(note_float + 0.5f);
   }
   
   } // namespace umidi::convert
