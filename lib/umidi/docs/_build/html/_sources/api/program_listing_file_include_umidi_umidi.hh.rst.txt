
.. _program_listing_file_include_umidi_umidi.hh:

Program Listing for File umidi.hh
=================================

|exhale_lsh| :ref:`Return to documentation for file <file_include_umidi_umidi.hh>` (``include/umidi/umidi.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   // UMI-OS MIDI Library - Main Header
   // High-performance MIDI 1.0/2.0 processing library (UMP-Opt format)
   //
   // This library is based on Super MIDI, optimized for ARM Cortex-M:
   // - UMP32 as single uint32_t for efficient comparison
   // - Single-mask type checking (is_note_on, is_cc, etc.)
   // - Incremental parser construction
   // - 40% memory reduction (8 bytes/event vs 20 bytes)
   //
   // Usage:
   //   #include <umidi/umidi.hh>
   //
   //   umidi::Parser parser;
   //   umidi::UMP32 ump;
   //   if (parser.parse(byte, ump)) {
   //       if (ump.is_note_on()) {
   //           // Handle Note On
   //       }
   //   }
   //
   #pragma once
   
   // Core types
   #include "core/ump.hh"
   #include "core/result.hh"
   #include "core/parser.hh"
   #include "core/sysex_buffer.hh"
   
   // Message types
   #include "messages/channel_voice.hh"
   #include "messages/system.hh"
   #include "messages/sysex.hh"
   #include "messages/utility.hh"
   
   // Control Change
   #include "cc/types.hh"
   #include "cc/standards.hh"
   #include "cc/decoder.hh"
   
   // Codec (template static decoder)
   #include "codec/decoder.hh"
   
   // UMI Protocol (Standard IO and Firmware Update over SysEx)
   #include "protocol/umi_sysex.hh"
   
   // Extended Protocol (Transport, State, Object Transfer)
   // Note: These must be included after umi_sysex.hh to avoid circular dependencies
   #include "protocol/umi_transport.hh"
   #include "protocol/umi_state.hh"
   #include "protocol/umi_object.hh"
   
   // Utilities
   #include "util/convert.hh"
   
   // Event integration
   #include "event.hh"
   
   namespace umidi {
   
   // Version information
   inline constexpr const char* version = "1.0.0";
   inline constexpr const char* description = "UMI-OS MIDI Library (UMP-Opt format)";
   
   // Convenience type aliases
   using MidiEvent = Event;
   using MidiQueue = EventQueue<256>;
   
   } // namespace umidi
