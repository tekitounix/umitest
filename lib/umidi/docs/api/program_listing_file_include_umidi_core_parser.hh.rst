
.. _program_listing_file_include_umidi_core_parser.hh:

Program Listing for File parser.hh
==================================

|exhale_lsh| :ref:`Return to documentation for file <file_include_umidi_core_parser.hh>` (``include/umidi/core/parser.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   #pragma once
   
   #include "ump.hh"
   #include "result.hh"
   #include <cstdint>
   
   namespace umidi {
   
   class Parser {
   public:
       constexpr Parser() noexcept = default;
   
       [[nodiscard]] constexpr bool parse(uint8_t byte, UMP32& out) noexcept {
           // Real-time messages (0xF8-0xFF): 1-byte, don't affect running status
           if (byte >= 0xF8) {
               out.word = 0x10000000u | (uint32_t(byte) << 16);
               return true;
           }
   
           // Status byte
           if (byte & 0x80) {
               // System common (0xF0-0xF7)
               if (byte >= 0xF0) {
                   return handle_system_common(byte, out);
               }
   
               // Channel message: start building UMP word
               // MT=2, status in bits 23-16
               partial_ = 0x20000000u | (uint32_t(byte) << 16);
               count_ = 0;
               running_status_ = byte;
   
               // 2-byte messages: 0xC0 (Program Change), 0xD0 (Channel Pressure)
               is_2byte_ = ((byte & 0xE0) == 0xC0);
               return false;
           }
   
           // Data byte
           if (count_ == 0) {
               // First data byte: bits 15-8
               partial_ |= (uint32_t(byte) << 8);
               count_ = 1;
   
               if (is_2byte_) {
                   out.word = partial_;
                   return true;
               }
               return false;
           }
   
           // Second data byte: bits 7-0
           out.word = partial_ | byte;
           count_ = 0;
           return true;
       }
   
       [[nodiscard]] constexpr bool parse_running(uint8_t byte, UMP32& out) noexcept {
           // Real-time: always process immediately
           if (byte >= 0xF8) {
               out.word = 0x10000000u | (uint32_t(byte) << 16);
               return true;
           }
   
           // Status byte
           if (byte & 0x80) {
               return parse(byte, out);
           }
   
           // Data byte with running status
           if (running_status_ == 0) {
               return false;  // No running status yet
           }
   
           // If we have no partial, restart message with running status
           if (count_ == 0 && (partial_ & 0x00FF0000u) == 0) {
               partial_ = 0x20000000u | (uint32_t(running_status_) << 16);
               is_2byte_ = ((running_status_ & 0xE0) == 0xC0);
           }
   
           // Process data byte
           if (count_ == 0) {
               partial_ |= (uint32_t(byte) << 8);
               count_ = 1;
   
               if (is_2byte_) {
                   out.word = partial_;
                   // Reset partial for next message but keep running status
                   partial_ = 0x20000000u | (uint32_t(running_status_) << 16);
                   count_ = 0;
                   return true;
               }
               return false;
           }
   
           // Second data byte
           out.word = partial_ | byte;
           // Reset for running status
           partial_ = 0x20000000u | (uint32_t(running_status_) << 16);
           count_ = 0;
           return true;
       }
   
       constexpr void reset() noexcept {
           partial_ = 0;
           count_ = 0;
           is_2byte_ = false;
           running_status_ = 0;
       }
   
       [[nodiscard]] constexpr uint8_t running_status() const noexcept {
           return running_status_;
       }
   
   private:
       uint32_t partial_ = 0;        // Partially constructed UMP word
       uint8_t running_status_ = 0;  // Running status byte
       uint8_t count_ = 0;           // Data byte count (0 or 1)
       bool is_2byte_ = false;       // True for 2-byte messages (PC, Channel Pressure)
   
       constexpr bool handle_system_common(uint8_t status, UMP32& out) noexcept {
           // Clear running status for system common
           running_status_ = 0;
   
           switch (status) {
           case 0xF0:  // SysEx Start - handled separately
           case 0xF7:  // SysEx End
               partial_ = 0;
               count_ = 0;
               return false;
   
           case 0xF1:  // MTC Quarter Frame (1 data byte)
           case 0xF3:  // Song Select (1 data byte)
               partial_ = 0x10000000u | (uint32_t(status) << 16);
               count_ = 0;
               is_2byte_ = true;
               return false;
   
           case 0xF2:  // Song Position (2 data bytes)
               partial_ = 0x10000000u | (uint32_t(status) << 16);
               count_ = 0;
               is_2byte_ = false;
               return false;
   
           case 0xF4:  // Undefined
           case 0xF5:  // Undefined
               return false;
   
           case 0xF6:  // Tune Request (no data)
               out.word = 0x10000000u | (uint32_t(status) << 16);
               return true;
   
           default:
               return false;
           }
       }
   };
   
   // =============================================================================
   // Serializer: UMP32 to MIDI 1.0 bytes
   // =============================================================================
   
   class Serializer {
   public:
       [[nodiscard]] static constexpr size_t serialize(const UMP32& ump, uint8_t* out) noexcept {
           uint32_t w = ump.word;
   
           // Check MT
           uint8_t mt = w >> 28;
           if (mt == 1) {
               // System message
               out[0] = (w >> 16) & 0xFF;
               uint8_t status = out[0];
   
               if (status >= 0xF8) return 1;  // Real-time
               if (status == 0xF6) return 1;  // Tune Request
               if (status == 0xF1 || status == 0xF3) {
                   out[1] = (w >> 8) & 0x7F;
                   return 2;
               }
               if (status == 0xF2) {
                   out[1] = (w >> 8) & 0x7F;
                   out[2] = w & 0x7F;
                   return 3;
               }
               return 0;
           }
   
           if (mt != 2) return 0;  // Only MT=2 (MIDI 1.0 CV) supported
   
           out[0] = (w >> 16) & 0xFF;
           out[1] = (w >> 8) & 0x7F;
   
           // 2-byte messages: 0xC0-0xDF
           uint8_t cmd = out[0] & 0xF0;
           if ((cmd & 0xE0) == 0xC0) return 2;
   
           out[2] = w & 0x7F;
           return 3;
       }
   
       [[nodiscard]] static constexpr size_t serialize_running(
           const UMP32& ump, uint8_t* out, uint8_t& running_status) noexcept
       {
           uint32_t w = ump.word;
           uint8_t mt = w >> 28;
   
           // System messages don't use running status
           if (mt == 1) {
               running_status = 0;
               return serialize(ump, out);
           }
   
           if (mt != 2) return 0;
   
           uint8_t status = (w >> 16) & 0xFF;
           uint8_t d1 = (w >> 8) & 0x7F;
           uint8_t d2 = w & 0x7F;
   
           // Check if we can use running status
           if (status == running_status) {
               out[0] = d1;
               if ((status & 0xE0) == 0xC0) return 1;  // 2-byte message
               out[1] = d2;
               return 2;
           }
   
           // Update running status and output full message
           running_status = status;
           out[0] = status;
           out[1] = d1;
           if ((status & 0xE0) == 0xC0) return 2;
           out[2] = d2;
           return 3;
       }
   };
   
   } // namespace umidi
