umidi Documentation
===================

umidi is a high-performance MIDI library designed for embedded systems,
particularly ARM Cortex-M microcontrollers.

Features
--------

- **UMP-Opt Format**: Single uint32_t representation for efficient comparison
- **Zero Allocation**: All operations work with pre-allocated buffers
- **Type-Safe Messages**: Compile-time message type checking
- **MIDI 1.0 & 2.0**: Support for both protocols via UMP
- **Embedded-First**: Designed for constrained environments

Quick Start
-----------

.. code-block:: cpp

   #include <umidi/core/ump.hh>
   #include <umidi/core/parser.hh>

   // Parse MIDI byte stream
   umidi::Parser parser;
   umidi::UMP32 ump;

   for (uint8_t byte : midi_input) {
       if (parser.parse(byte, ump)) {
           if (ump.is_note_on()) {
               handle_note_on(ump.note(), ump.velocity());
           }
       }
   }

.. toctree::
   :maxdepth: 2
   :caption: Contents:

   README
   design
   examples
   CORE
   MESSAGES
   CC
   PROTOCOL
   TRANSPORT
   STATE
   OBJECT
   API
   api/library_root

Indices and tables
==================

* :ref:`genindex`
* :ref:`search`
