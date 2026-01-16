
.. _program_listing_file_messages_channel_voice.hh:

Program Listing for File channel_voice.hh
=========================================

|exhale_lsh| :ref:`Return to documentation for file <file_messages_channel_voice.hh>` (``messages/channel_voice.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   #pragma once
   
   #include "../core/ump.hh"
   #include <cstdint>
   
   namespace umidi::message {
   
   struct NoteOn {
       UMP32 ump;
   
       static constexpr uint8_t MT = 2;
       static constexpr uint8_t STATUS = 0x90;
   
       // Accessors
       [[nodiscard]] constexpr uint8_t channel() const noexcept { return ump.channel(); }
       [[nodiscard]] constexpr uint8_t note() const noexcept { return ump.note(); }
       [[nodiscard]] constexpr uint8_t velocity() const noexcept { return ump.velocity(); }
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       // Check validity
       [[nodiscard]] constexpr bool is_valid() const noexcept { return ump.is_note_on(); }
   
       // Factory method
       [[nodiscard]] static constexpr NoteOn
       create(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t group = 0) noexcept {
           return {UMP32::note_on(channel, note, velocity, group)};
       }
   
       // From existing UMP (assumes caller has verified type)
       [[nodiscard]] static constexpr NoteOn from_ump(UMP32 u) noexcept { return {u}; }
   };
   
   // Note Off message
   struct NoteOff {
       UMP32 ump;
   
       static constexpr uint8_t MT = 2;
       static constexpr uint8_t STATUS = 0x80;
   
       [[nodiscard]] constexpr uint8_t channel() const noexcept { return ump.channel(); }
       [[nodiscard]] constexpr uint8_t note() const noexcept { return ump.note(); }
       [[nodiscard]] constexpr uint8_t velocity() const noexcept { return ump.velocity(); }
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       [[nodiscard]] constexpr bool is_valid() const noexcept { return ump.is_note_off(); }
   
       [[nodiscard]] static constexpr NoteOff
       create(uint8_t channel, uint8_t note, uint8_t velocity = 0, uint8_t group = 0) noexcept {
           return {UMP32::note_off(channel, note, velocity, group)};
       }
   
       [[nodiscard]] static constexpr NoteOff from_ump(UMP32 u) noexcept { return {u}; }
   };
   
   // Control Change message
   struct ControlChange {
       UMP32 ump;
   
       static constexpr uint8_t MT = 2;
       static constexpr uint8_t STATUS = 0xB0;
   
       [[nodiscard]] constexpr uint8_t channel() const noexcept { return ump.channel(); }
       [[nodiscard]] constexpr uint8_t controller() const noexcept { return ump.cc_number(); }
       [[nodiscard]] constexpr uint8_t value() const noexcept { return ump.cc_value(); }
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       [[nodiscard]] constexpr bool is_valid() const noexcept { return ump.is_cc(); }
   
       [[nodiscard]] static constexpr ControlChange
       create(uint8_t channel, uint8_t controller, uint8_t value, uint8_t group = 0) noexcept {
           return {UMP32::cc(channel, controller, value, group)};
       }
   
       [[nodiscard]] static constexpr ControlChange from_ump(UMP32 u) noexcept { return {u}; }
   };
   
   // Program Change message
   struct ProgramChange {
       UMP32 ump;
   
       static constexpr uint8_t MT = 2;
       static constexpr uint8_t STATUS = 0xC0;
   
       [[nodiscard]] constexpr uint8_t channel() const noexcept { return ump.channel(); }
       [[nodiscard]] constexpr uint8_t program() const noexcept { return ump.data1(); }
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       [[nodiscard]] constexpr bool is_valid() const noexcept { return ump.is_program_change(); }
   
       [[nodiscard]] static constexpr ProgramChange
       create(uint8_t channel, uint8_t program, uint8_t group = 0) noexcept {
           return {UMP32::program_change(channel, program, group)};
       }
   
       [[nodiscard]] static constexpr ProgramChange from_ump(UMP32 u) noexcept { return {u}; }
   };
   
   // Channel Pressure (Aftertouch) message
   struct ChannelPressure {
       UMP32 ump;
   
       static constexpr uint8_t MT = 2;
       static constexpr uint8_t STATUS = 0xD0;
   
       [[nodiscard]] constexpr uint8_t channel() const noexcept { return ump.channel(); }
       [[nodiscard]] constexpr uint8_t pressure() const noexcept { return ump.data1(); }
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       [[nodiscard]] constexpr bool is_valid() const noexcept { return ump.is_channel_pressure(); }
   
       [[nodiscard]] static constexpr ChannelPressure
       create(uint8_t channel, uint8_t pressure, uint8_t group = 0) noexcept {
           return {UMP32(2, group, 0xD0 | (channel & 0x0F), pressure & 0x7F, 0)};
       }
   
       [[nodiscard]] static constexpr ChannelPressure from_ump(UMP32 u) noexcept { return {u}; }
   };
   
   // Pitch Bend message
   struct PitchBend {
       UMP32 ump;
   
       static constexpr uint8_t MT = 2;
       static constexpr uint8_t STATUS = 0xE0;
   
       [[nodiscard]] constexpr uint8_t channel() const noexcept { return ump.channel(); }
       [[nodiscard]] constexpr uint16_t value() const noexcept { return ump.pitch_bend_value(); }
       [[nodiscard]] constexpr int16_t signed_value() const noexcept {
           return static_cast<int16_t>(value()) - 8192;
       }
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       [[nodiscard]] constexpr bool is_valid() const noexcept { return ump.is_pitch_bend(); }
   
       [[nodiscard]] static constexpr PitchBend
       create(uint8_t channel, uint16_t value, uint8_t group = 0) noexcept {
           return {UMP32::pitch_bend(channel, value, group)};
       }
   
       // Create from signed value (-8192 to +8191)
       [[nodiscard]] static constexpr PitchBend
       create_signed(uint8_t channel, int16_t value, uint8_t group = 0) noexcept {
           uint16_t unsigned_val = static_cast<uint16_t>(value + 8192);
           return {UMP32::pitch_bend(channel, unsigned_val, group)};
       }
   
       [[nodiscard]] static constexpr PitchBend from_ump(UMP32 u) noexcept { return {u}; }
   };
   
   // Polyphonic Key Pressure message
   struct PolyPressure {
       UMP32 ump;
   
       static constexpr uint8_t MT = 2;
       static constexpr uint8_t STATUS = 0xA0;
   
       [[nodiscard]] constexpr uint8_t channel() const noexcept { return ump.channel(); }
       [[nodiscard]] constexpr uint8_t note() const noexcept { return ump.note(); }
       [[nodiscard]] constexpr uint8_t pressure() const noexcept { return ump.data2(); }
       [[nodiscard]] constexpr uint8_t group() const noexcept { return ump.group(); }
   
       [[nodiscard]] constexpr bool is_valid() const noexcept { return ump.is_poly_pressure(); }
   
       [[nodiscard]] static constexpr PolyPressure
       create(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t group = 0) noexcept {
           return {UMP32(2, group, 0xA0 | (channel & 0x0F), note & 0x7F, pressure & 0x7F)};
       }
   
       [[nodiscard]] static constexpr PolyPressure from_ump(UMP32 u) noexcept { return {u}; }
   };
   
   // =============================================================================
   // Message dispatch helper
   // =============================================================================
   
   template <typename Handler>
   constexpr void dispatch(const UMP32& ump, Handler&& handler) noexcept {
       if (ump.is_note_on()) {
           handler(NoteOn::from_ump(ump));
       } else if (ump.is_note_off()) {
           handler(NoteOff::from_ump(ump));
       } else if (ump.is_cc()) {
           handler(ControlChange::from_ump(ump));
       } else if (ump.is_program_change()) {
           handler(ProgramChange::from_ump(ump));
       } else if (ump.is_channel_pressure()) {
           handler(ChannelPressure::from_ump(ump));
       } else if (ump.is_pitch_bend()) {
           handler(PitchBend::from_ump(ump));
       } else if (ump.is_poly_pressure()) {
           handler(PolyPressure::from_ump(ump));
       }
   }
   
   } // namespace umidi::message
