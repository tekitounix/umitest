
.. _program_listing_file_include_umidi_messages_utility.hh:

Program Listing for File utility.hh
===================================

|exhale_lsh| :ref:`Return to documentation for file <file_include_umidi_messages_utility.hh>` (``include/umidi/messages/utility.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   // umidi - Utility Messages (MT=0, MIDI 2.0)
   // JR Timestamp, JR Clock, NOOP for timing jitter correction
   #pragma once
   
   #include "../core/ump.hh"
   #include <cstdint>
   
   namespace umidi::message {
   
   // =============================================================================
   // JR Timestamp: Jitter Reduction Timestamp (MT=0, 0x0020)
   // =============================================================================
   // Provides sample-accurate timing for subsequent messages
   // Timestamp unit: 1/31250 second (32 microseconds)
   
   struct JRTimestamp {
       UMP32 ump;
   
       static constexpr uint8_t MT = 0;
       static constexpr uint8_t STATUS = 0x20;
   
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       [[nodiscard]] constexpr uint16_t timestamp() const noexcept {
           return uint16_t(ump.data1()) | (uint16_t(ump.data2()) << 7);
       }
   
       [[nodiscard]] constexpr uint32_t to_microseconds() const noexcept {
           return uint32_t(timestamp()) * 32u;
       }
   
       [[nodiscard]] constexpr bool is_valid() const noexcept {
           return ump.mt() == MT && ump.status() == STATUS;
       }
   
       [[nodiscard]] static constexpr JRTimestamp create(uint16_t ts, uint8_t group = 0) noexcept {
           return {UMP32(MT, group, STATUS, ts & 0x7F, (ts >> 7) & 0x7F)};
       }
   
       [[nodiscard]] static constexpr JRTimestamp from_microseconds(uint32_t us, uint8_t group = 0) noexcept {
           uint16_t ts = uint16_t((us / 32u) & 0x3FFF);
           return create(ts, group);
       }
   
       [[nodiscard]] static constexpr JRTimestamp from_ump(UMP32 u) noexcept { return {u}; }
   };
   
   // =============================================================================
   // JR Clock: Jitter Reduction Clock (MT=0, 0x0010)
   // =============================================================================
   // Sender's clock for synchronization
   
   struct JRClock {
       UMP32 ump;
   
       static constexpr uint8_t MT = 0;
       static constexpr uint8_t STATUS = 0x10;
   
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       [[nodiscard]] constexpr uint16_t clock() const noexcept {
           return uint16_t(ump.data1()) | (uint16_t(ump.data2()) << 7);
       }
   
       [[nodiscard]] constexpr bool is_valid() const noexcept {
           return ump.mt() == MT && ump.status() == STATUS;
       }
   
       [[nodiscard]] static constexpr JRClock create(uint16_t clk, uint8_t group = 0) noexcept {
           return {UMP32(MT, group, STATUS, clk & 0x7F, (clk >> 7) & 0x7F)};
       }
   
       [[nodiscard]] static constexpr JRClock from_ump(UMP32 u) noexcept { return {u}; }
   };
   
   // =============================================================================
   // NOOP: No Operation (MT=0, 0x0000)
   // =============================================================================
   // For maintaining connection or padding
   
   struct NOOP {
       UMP32 ump;
   
       static constexpr uint8_t MT = 0;
       static constexpr uint8_t STATUS = 0x00;
   
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       [[nodiscard]] constexpr bool is_valid() const noexcept {
           return ump.mt() == MT && ump.status() == STATUS;
       }
   
       [[nodiscard]] static constexpr NOOP create(uint8_t group = 0) noexcept {
           return {UMP32(MT, group, STATUS, 0, 0)};
       }
   
       [[nodiscard]] static constexpr NOOP from_ump(UMP32 u) noexcept { return {u}; }
   };
   
   // =============================================================================
   // JR Timestamp Tracker: Track and apply jitter reduction
   // =============================================================================
   // Converts JR timestamps to sample positions for audio processing
   
   class JRTimestampTracker {
   public:
       constexpr void set_sample_rate(uint32_t rate) noexcept {
           sample_rate_ = rate;
           // 1 JR tick = 32us = 32/1000000 seconds
           // samples per JR tick = sample_rate * 32 / 1000000
           // Use fixed-point: multiply by 32, divide by 1000000
           samples_per_tick_fp_ = (rate * 32u) / 1000u;  // Fixed-point * 1000
       }
   
       constexpr void process(const JRTimestamp& ts) noexcept {
           last_timestamp_ = ts.timestamp();
           has_timestamp_ = true;
       }
   
       [[nodiscard]] constexpr uint32_t get_sample_offset() const noexcept {
           if (!has_timestamp_) return 0;
           // Convert JR ticks to samples
           return (last_timestamp_ * samples_per_tick_fp_) / 1000u;
       }
   
       constexpr void clear() noexcept {
           has_timestamp_ = false;
       }
   
       [[nodiscard]] constexpr bool has_timestamp() const noexcept {
           return has_timestamp_;
       }
   
   private:
       uint32_t sample_rate_ = 48000;
       uint32_t samples_per_tick_fp_ = (48000 * 32u) / 1000u;
       uint16_t last_timestamp_ = 0;
       bool has_timestamp_ = false;
   };
   
   } // namespace umidi::message
