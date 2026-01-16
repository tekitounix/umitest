
.. _program_listing_file_include_umidi_protocol_commands.hh:

Program Listing for File commands.hh
====================================

|exhale_lsh| :ref:`Return to documentation for file <file_include_umidi_protocol_commands.hh>` (``include/umidi/protocol/commands.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   #pragma once
   
   #include <cstdint>
   
   namespace umidi::protocol {
   
   constexpr uint8_t UMI_SYSEX_ID[] = {0x7E, 0x7F, 0x00};
   constexpr size_t UMI_SYSEX_ID_LEN = 3;
   
   constexpr uint8_t PROTOCOL_VERSION_MAJOR = 1;
   constexpr uint8_t PROTOCOL_VERSION_MINOR = 0;
   
   enum class Command : uint8_t {
       // Standard IO (0x01-0x0F)
       STDOUT_DATA = 0x01,
       STDERR_DATA = 0x02,
       STDIN_DATA  = 0x03,
       STDIN_EOF   = 0x04,
       FLOW_CTRL   = 0x05,
   
       // Firmware Update (0x10-0x1F)
       FW_QUERY    = 0x10,
       FW_INFO     = 0x11,
       FW_BEGIN    = 0x12,
       FW_DATA     = 0x13,
       FW_VERIFY   = 0x14,
       FW_COMMIT   = 0x15,
       FW_ROLLBACK = 0x16,
       FW_REBOOT   = 0x17,
       FW_ACK      = 0x18,
       FW_NACK     = 0x19,
   
       // System (0x20-0x2F)
       PING        = 0x20,
       PONG        = 0x21,
       RESET       = 0x22,
       VERSION     = 0x23,
   };
   
   enum class ErrorCode : uint8_t {
       OK                  = 0x00,
       INVALID_COMMAND     = 0x01,
       INVALID_SEQUENCE    = 0x02,
       INVALID_CHECKSUM    = 0x03,
       BUFFER_OVERFLOW     = 0x04,
       UPDATE_NOT_STARTED  = 0x05,
       UPDATE_IN_PROGRESS  = 0x06,
       VERIFY_FAILED       = 0x07,
       SIGNATURE_INVALID   = 0x08,
       FLASH_ERROR         = 0x09,
       ROLLBACK_UNAVAIL    = 0x0A,
       TIMEOUT             = 0x0B,
   };
   
   enum class FlowControl : uint8_t {
       XON  = 0x11,  
       XOFF = 0x13,  
   };
   
   // =============================================================================
   // Callback Types
   // =============================================================================
   
   using FlashWriteCallback = bool (*)(uint32_t offset, const uint8_t* data,
                                       size_t len, void* ctx);
   
   using SignatureVerifyCallback = bool (*)(const uint8_t* data, size_t len,
                                            const uint8_t* signature, void* ctx);
   
   } // namespace umidi::protocol
