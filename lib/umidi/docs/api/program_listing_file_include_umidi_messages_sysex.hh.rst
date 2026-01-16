
.. _program_listing_file_include_umidi_messages_sysex.hh:

Program Listing for File sysex.hh
=================================

|exhale_lsh| :ref:`Return to documentation for file <file_include_umidi_messages_sysex.hh>` (``include/umidi/messages/sysex.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   // umidi - SysEx Messages (UMP64 format)
   #pragma once
   
   #include "../core/ump.hh"
   #include <cstdint>
   #include <cstddef>
   
   namespace umidi::message {
   
   // =============================================================================
   // SysEx7: 7-bit System Exclusive (MT=3, UMP64)
   // =============================================================================
   // Packs up to 6 bytes per UMP64 packet
   
   struct SysEx7 {
       UMP64 ump;
   
       enum class Status : uint8_t {
           COMPLETE = 0x00,  // Complete SysEx in one packet
           START    = 0x10,  // Start of multi-packet SysEx
           CONTINUE = 0x20,  // Continuation packet
           END      = 0x30   // End of multi-packet SysEx
       };
   
       // === Accessors ===
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       [[nodiscard]] constexpr Status sysex_status() const noexcept {
           return static_cast<Status>((ump.word0 >> 16) & 0xF0);
       }
   
       [[nodiscard]] constexpr uint8_t num_bytes() const noexcept {
           return (ump.word0 >> 16) & 0x0F;
       }
   
       [[nodiscard]] constexpr bool is_valid() const noexcept {
           return ump.mt() == 3;
       }
   
       [[nodiscard]] constexpr uint8_t data_at(size_t idx) const noexcept {
           switch (idx) {
           case 0: return (ump.word0 >> 8) & 0xFF;
           case 1: return ump.word0 & 0xFF;
           case 2: return (ump.word1 >> 24) & 0xFF;
           case 3: return (ump.word1 >> 16) & 0xFF;
           case 4: return (ump.word1 >> 8) & 0xFF;
           case 5: return ump.word1 & 0xFF;
           default: return 0;
           }
       }
   
       constexpr size_t copy_data(uint8_t* out, size_t max_len) const noexcept {
           size_t count = num_bytes();
           if (count > 6) count = 6;
           if (count > max_len) count = max_len;
   
           for (size_t i = 0; i < count; ++i) {
               out[i] = data_at(i);
           }
           return count;
       }
   
       // === Factory methods ===
       [[nodiscard]] static constexpr SysEx7
       create(Status status, uint8_t group, const uint8_t* data, size_t len) noexcept {
           SysEx7 msg{};
           size_t count = (len > 6) ? 6 : len;
   
           // MT=3, Group, Status|Count
           msg.ump.word0 = (3u << 28) | ((group & 0x0Fu) << 24) |
                           ((static_cast<uint8_t>(status) | count) << 16);
   
           // Pack data bytes
           if (count >= 1) msg.ump.word0 |= (uint32_t(data[0]) << 8);
           if (count >= 2) msg.ump.word0 |= data[1];
           if (count >= 3) msg.ump.word1 |= (uint32_t(data[2]) << 24);
           if (count >= 4) msg.ump.word1 |= (uint32_t(data[3]) << 16);
           if (count >= 5) msg.ump.word1 |= (uint32_t(data[4]) << 8);
           if (count >= 6) msg.ump.word1 |= data[5];
   
           return msg;
       }
   
       [[nodiscard]] static constexpr SysEx7
       create_complete(uint8_t group, const uint8_t* data, size_t len) noexcept {
           return create(Status::COMPLETE, group, data, len);
       }
   
       [[nodiscard]] static constexpr SysEx7
       create_start(uint8_t group, const uint8_t* data, size_t len) noexcept {
           return create(Status::START, group, data, len);
       }
   
       [[nodiscard]] static constexpr SysEx7
       create_continue(uint8_t group, const uint8_t* data, size_t len) noexcept {
           return create(Status::CONTINUE, group, data, len);
       }
   
       [[nodiscard]] static constexpr SysEx7
       create_end(uint8_t group, const uint8_t* data, size_t len) noexcept {
           return create(Status::END, group, data, len);
       }
   
       [[nodiscard]] static constexpr SysEx7 from_ump(UMP64 u) noexcept { return {u}; }
   };
   
   // =============================================================================
   // SysEx Parser: MIDI 1.0 byte stream to UMP64 SysEx7
   // =============================================================================
   
   template <size_t MaxSysExSize = 256>
   class SysExParser {
   public:
       struct Result {
           SysEx7 packet;
           bool complete = false;  // True if packet is ready
       };
   
       constexpr SysExParser() noexcept = default;
   
       [[nodiscard]] constexpr Result parse(uint8_t byte, uint8_t group = 0) noexcept {
           Result result{};
   
           // SysEx Start (0xF0)
           if (byte == 0xF0) {
               reset();
               in_sysex_ = true;
               return result;
           }
   
           // SysEx End (0xF7)
           if (byte == 0xF7) {
               if (in_sysex_) {
                   result = flush_packet(group, true);
                   reset();
               }
               return result;
           }
   
           // Data byte (ignore if not in SysEx)
           if (in_sysex_ && (byte & 0x80) == 0) {
               if (count_ < 6) {
                   buffer_[count_++] = byte;
               }
   
               // Buffer full, emit continuation packet
               if (count_ == 6) {
                   result = flush_packet(group, false);
               }
           }
   
           return result;
       }
   
       [[nodiscard]] constexpr bool in_sysex() const noexcept { return in_sysex_; }
   
       constexpr void reset() noexcept {
           count_ = 0;
           packet_count_ = 0;
           in_sysex_ = false;
       }
   
   private:
       uint8_t buffer_[6] = {};
       size_t count_ = 0;
       size_t packet_count_ = 0;
       bool in_sysex_ = false;
   
       [[nodiscard]] constexpr Result flush_packet(uint8_t group, bool is_end) noexcept {
           Result result{};
   
           SysEx7::Status status;
           if (packet_count_ == 0 && is_end) {
               status = SysEx7::Status::COMPLETE;
           } else if (packet_count_ == 0) {
               status = SysEx7::Status::START;
           } else if (is_end) {
               status = SysEx7::Status::END;
           } else {
               status = SysEx7::Status::CONTINUE;
           }
   
           result.packet = SysEx7::create(status, group, buffer_, count_);
           result.complete = true;
   
           count_ = 0;
           ++packet_count_;
   
           return result;
       }
   };
   
   // =============================================================================
   // SysEx Serializer: UMP64 SysEx7 to MIDI 1.0 byte stream
   // =============================================================================
   
   class SysExSerializer {
   public:
       [[nodiscard]] static constexpr size_t serialize(
           const SysEx7& packet, uint8_t* out, bool include_markers = false) noexcept
       {
           size_t pos = 0;
   
           // Add F0 for START or COMPLETE
           if (include_markers) {
               auto status = packet.sysex_status();
               if (status == SysEx7::Status::START || status == SysEx7::Status::COMPLETE) {
                   out[pos++] = 0xF0;
               }
           }
   
           // Copy data bytes
           size_t count = packet.num_bytes();
           if (count > 6) count = 6;
           for (size_t i = 0; i < count; ++i) {
               out[pos++] = packet.data_at(i);
           }
   
           // Add F7 for END or COMPLETE
           if (include_markers) {
               auto status = packet.sysex_status();
               if (status == SysEx7::Status::END || status == SysEx7::Status::COMPLETE) {
                   out[pos++] = 0xF7;
               }
           }
   
           return pos;
       }
   };
   
   } // namespace umidi::message
