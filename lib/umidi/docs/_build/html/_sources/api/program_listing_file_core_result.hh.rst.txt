
.. _program_listing_file_core_result.hh:

Program Listing for File result.hh
==================================

|exhale_lsh| :ref:`Return to documentation for file <file_core_result.hh>` (``core/result.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   // UMI-OS MIDI Library - Result Type
   #pragma once
   
   #include <cstdint>
   #include <expected>
   #include <string_view>
   
   namespace umidi {
   
   // Error codes for MIDI operations
   enum class ErrorCode : uint8_t {
       Ok,
       IncompleteMessage,
       InvalidStatus,
       BufferOverflow,
       InvalidData,
       InvalidMessageType,
       ChannelFiltered,
       NotImplemented,
       NotSupported
   };
   
   // Lightweight error type (no heap allocation)
   struct Error {
       ErrorCode code = ErrorCode::IncompleteMessage;
       uint8_t context = 0;  // Optional context byte
   
       constexpr Error() noexcept = default;
       constexpr Error(ErrorCode c) noexcept : code(c), context(0) {}
       constexpr Error(ErrorCode c, uint8_t ctx) noexcept : code(c), context(ctx) {}
   
       // Factory methods
       static constexpr Error incomplete() noexcept {
           return {ErrorCode::IncompleteMessage};
       }
   
       static constexpr Error incomplete(uint8_t byte) noexcept {
           return {ErrorCode::IncompleteMessage, byte};
       }
   
       static constexpr Error invalid_status(uint8_t status) noexcept {
           return {ErrorCode::InvalidStatus, status};
       }
   
       static constexpr Error buffer_overflow() noexcept {
           return {ErrorCode::BufferOverflow};
       }
   
       static constexpr Error invalid_data(uint8_t data) noexcept {
           return {ErrorCode::InvalidData, data};
       }
   
       static constexpr Error channel_filtered(uint8_t ch) noexcept {
           return {ErrorCode::ChannelFiltered, ch};
       }
   
       static constexpr Error not_implemented() noexcept {
           return {ErrorCode::NotImplemented};
       }
   };
   
   // Result type using std::expected
   template <typename T>
   using Result = std::expected<T, Error>;
   
   // =============================================================================
   // Helper functions for Result construction
   // =============================================================================
   
   template <typename T>
   [[nodiscard]] constexpr Result<T> Ok(T value) noexcept {
       return Result<T>(std::move(value));
   }
   
   template <typename T = void>
   [[nodiscard]] constexpr std::unexpected<Error> Err(Error error) noexcept {
       return std::unexpected<Error>(error);
   }
   
   template <typename T = void>
   [[nodiscard]] constexpr std::unexpected<Error> Err(ErrorCode code) noexcept {
       return std::unexpected<Error>(Error(code));
   }
   
   } // namespace umidi
