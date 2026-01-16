
.. _program_listing_file_include_umidi_event.hh:

Program Listing for File event.hh
=================================

|exhale_lsh| :ref:`Return to documentation for file <file_include_umidi_event.hh>` (``include/umidi/event.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   // UMI-OS MIDI Library - Sample-Accurate Event
   // Integrates UMP32 with sample position for audio processing
   #pragma once
   
   #include "core/ump.hh"
   #include <cstdint>
   #include <cstddef>
   
   namespace umidi {
   
   // =============================================================================
   // Event: Sample-Accurate MIDI Event (UMP-Opt format)
   // =============================================================================
   // Size: 8 bytes (vs 20 bytes in old format)
   // - sample_pos: 4 bytes (sample position within buffer)
   // - ump: 4 bytes (UMP32)
   
   struct Event {
       uint32_t sample_pos = 0;  
       UMP32 ump;                
   
       constexpr Event() noexcept = default;
       constexpr Event(uint32_t pos, UMP32 u) noexcept : sample_pos(pos), ump(u) {}
   
       // === Convenience accessors (delegate to UMP32) ===
       [[nodiscard]] constexpr bool is_note_on() const noexcept { return ump.is_note_on(); }
       [[nodiscard]] constexpr bool is_note_off() const noexcept { return ump.is_note_off(); }
       [[nodiscard]] constexpr bool is_cc() const noexcept { return ump.is_cc(); }
       [[nodiscard]] constexpr bool is_program_change() const noexcept { return ump.is_program_change(); }
       [[nodiscard]] constexpr bool is_pitch_bend() const noexcept { return ump.is_pitch_bend(); }
       [[nodiscard]] constexpr bool is_channel_pressure() const noexcept { return ump.is_channel_pressure(); }
       [[nodiscard]] constexpr bool is_poly_pressure() const noexcept { return ump.is_poly_pressure(); }
       [[nodiscard]] constexpr bool is_realtime() const noexcept { return ump.is_realtime(); }
       [[nodiscard]] constexpr bool is_system() const noexcept { return ump.is_system(); }
   
       [[nodiscard]] constexpr uint8_t channel() const noexcept { return ump.channel(); }
       [[nodiscard]] constexpr uint8_t note() const noexcept { return ump.note(); }
       [[nodiscard]] constexpr uint8_t velocity() const noexcept { return ump.velocity(); }
       [[nodiscard]] constexpr uint8_t cc_number() const noexcept { return ump.cc_number(); }
       [[nodiscard]] constexpr uint8_t cc_value() const noexcept { return ump.cc_value(); }
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       // === Factory methods ===
       [[nodiscard]] static constexpr Event
       note_on(uint32_t pos, uint8_t channel, uint8_t note, uint8_t velocity, uint8_t group = 0) noexcept {
           return {pos, UMP32::note_on(channel, note, velocity, group)};
       }
   
       [[nodiscard]] static constexpr Event
       note_off(uint32_t pos, uint8_t channel, uint8_t note, uint8_t velocity = 0, uint8_t group = 0) noexcept {
           return {pos, UMP32::note_off(channel, note, velocity, group)};
       }
   
       [[nodiscard]] static constexpr Event
       cc(uint32_t pos, uint8_t channel, uint8_t cc_num, uint8_t value, uint8_t group = 0) noexcept {
           return {pos, UMP32::cc(channel, cc_num, value, group)};
       }
   
       [[nodiscard]] static constexpr Event
       program_change(uint32_t pos, uint8_t channel, uint8_t program, uint8_t group = 0) noexcept {
           return {pos, UMP32::program_change(channel, program, group)};
       }
   
       [[nodiscard]] static constexpr Event
       pitch_bend(uint32_t pos, uint8_t channel, uint16_t value, uint8_t group = 0) noexcept {
           return {pos, UMP32::pitch_bend(channel, value, group)};
       }
   
       [[nodiscard]] static constexpr Event
       timing_clock(uint32_t pos, uint8_t group = 0) noexcept {
           return {pos, UMP32::timing_clock(group)};
       }
   
       [[nodiscard]] static constexpr Event
       start(uint32_t pos, uint8_t group = 0) noexcept {
           return {pos, UMP32::start(group)};
       }
   
       [[nodiscard]] static constexpr Event
       stop(uint32_t pos, uint8_t group = 0) noexcept {
           return {pos, UMP32::stop(group)};
       }
   
       // === Comparison (for sorting by sample position) ===
       constexpr bool operator<(const Event& other) const noexcept {
           return sample_pos < other.sample_pos;
       }
   };
   
   static_assert(sizeof(Event) == 8, "Event must be 8 bytes");
   
   // =============================================================================
   // EventQueue: Lock-free SPSC ring buffer for events
   // =============================================================================
   
   template <size_t Capacity = 256>
   class EventQueue {
   public:
       constexpr EventQueue() noexcept = default;
   
       [[nodiscard]] bool push(const Event& e) noexcept {
           const size_t next = (write_pos_ + 1) % Capacity;
           if (next == read_pos_) {
               return false;  // Full
           }
           events_[write_pos_] = e;
           write_pos_ = next;
           return true;
       }
   
       [[nodiscard]] bool push_note_on(uint32_t pos, uint8_t ch, uint8_t note, uint8_t vel) noexcept {
           return push(Event::note_on(pos, ch, note, vel));
       }
   
       [[nodiscard]] bool push_note_off(uint32_t pos, uint8_t ch, uint8_t note, uint8_t vel = 0) noexcept {
           return push(Event::note_off(pos, ch, note, vel));
       }
   
       [[nodiscard]] bool push_cc(uint32_t pos, uint8_t ch, uint8_t cc, uint8_t val) noexcept {
           return push(Event::cc(pos, ch, cc, val));
       }
   
       [[nodiscard]] bool pop(Event& out) noexcept {
           if (read_pos_ == write_pos_) {
               return false;  // Empty
           }
           out = events_[read_pos_];
           read_pos_ = (read_pos_ + 1) % Capacity;
           return true;
       }
   
       [[nodiscard]] bool pop_until(uint32_t sample_pos, Event& out) noexcept {
           if (read_pos_ == write_pos_) {
               return false;
           }
           if (events_[read_pos_].sample_pos > sample_pos) {
               return false;
           }
           return pop(out);
       }
   
       [[nodiscard]] bool peek(Event& out) const noexcept {
           if (read_pos_ == write_pos_) {
               return false;
           }
           out = events_[read_pos_];
           return true;
       }
   
       [[nodiscard]] bool peek_sample_pos(uint32_t& pos) const noexcept {
           if (read_pos_ == write_pos_) {
               return false;
           }
           pos = events_[read_pos_].sample_pos;
           return true;
       }
   
       [[nodiscard]] bool empty() const noexcept {
           return read_pos_ == write_pos_;
       }
   
       [[nodiscard]] size_t size() const noexcept {
           if (write_pos_ >= read_pos_) {
               return write_pos_ - read_pos_;
           }
           return Capacity - read_pos_ + write_pos_;
       }
   
       [[nodiscard]] size_t available() const noexcept {
           return Capacity - size() - 1;
       }
   
       void clear() noexcept {
           read_pos_ = 0;
           write_pos_ = 0;
       }
   
       [[nodiscard]] static constexpr size_t capacity() noexcept {
           return Capacity - 1;  // One slot reserved for full detection
       }
   
   private:
       Event events_[Capacity] = {};
       size_t read_pos_ = 0;
       size_t write_pos_ = 0;
   };
   
   // =============================================================================
   // Compatibility with old umi::Event
   // =============================================================================
   
   namespace compat {
   
   [[nodiscard]] constexpr Event
   from_midi_bytes(uint32_t sample_pos, uint8_t status, uint8_t d1, uint8_t d2 = 0) noexcept {
       uint8_t cmd = status & 0xF0;
       uint8_t ch = status & 0x0F;
   
       // Determine message type
       if (cmd == 0x80) {
           return Event::note_off(sample_pos, ch, d1, d2);
       } else if (cmd == 0x90) {
           if (d2 == 0) {
               return Event::note_off(sample_pos, ch, d1, 0);
           }
           return Event::note_on(sample_pos, ch, d1, d2);
       } else if (cmd == 0xA0) {
           return {sample_pos, UMP32(2, 0, status, d1, d2)};  // Poly pressure
       } else if (cmd == 0xB0) {
           return Event::cc(sample_pos, ch, d1, d2);
       } else if (cmd == 0xC0) {
           return Event::program_change(sample_pos, ch, d1);
       } else if (cmd == 0xD0) {
           return {sample_pos, UMP32(2, 0, status, d1, 0)};  // Channel pressure
       } else if (cmd == 0xE0) {
           uint16_t pb = uint16_t(d1) | (uint16_t(d2) << 7);
           return Event::pitch_bend(sample_pos, ch, pb);
       } else if (status >= 0xF8) {
           return {sample_pos, UMP32(1, 0, status, 0, 0)};  // Realtime
       }
   
       // Unknown - return as-is
       return {sample_pos, UMP32(2, 0, status, d1, d2)};
   }
   
   constexpr size_t to_midi_bytes(const Event& e, uint8_t* out) noexcept {
       uint32_t w = e.ump.word;
       uint8_t mt = w >> 28;
   
       if (mt == 1) {
           // System message
           out[0] = (w >> 16) & 0xFF;
           if (out[0] >= 0xF8) return 1;
           if (out[0] == 0xF6) return 1;
           if (out[0] == 0xF1 || out[0] == 0xF3) {
               out[1] = (w >> 8) & 0x7F;
               return 2;
           }
           if (out[0] == 0xF2) {
               out[1] = (w >> 8) & 0x7F;
               out[2] = w & 0x7F;
               return 3;
           }
           return 0;
       }
   
       if (mt != 2) return 0;
   
       out[0] = (w >> 16) & 0xFF;
       out[1] = (w >> 8) & 0x7F;
   
       uint8_t cmd = out[0] & 0xF0;
       if ((cmd & 0xE0) == 0xC0) return 2;
   
       out[2] = w & 0x7F;
       return 3;
   }
   
   } // namespace compat
   
   } // namespace umidi
