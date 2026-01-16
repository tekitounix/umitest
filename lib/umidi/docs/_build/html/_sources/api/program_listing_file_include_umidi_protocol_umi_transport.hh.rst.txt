
.. _program_listing_file_include_umidi_protocol_umi_transport.hh:

Program Listing for File umi_transport.hh
=========================================

|exhale_lsh| :ref:`Return to documentation for file <file_include_umidi_protocol_umi_transport.hh>` (``include/umidi/protocol/umi_transport.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   // UMI-OS MIDI Library - Transport Abstraction
   // Unified interface for SysEx (WebMIDI) and Bulk (WebUSB) transports
   #pragma once
   
   #include "../core/ump.hh"
   #include "../messages/sysex.hh"
   #include "umi_sysex.hh"
   #include <cstdint>
   #include <cstddef>
   #include <span>
   
   namespace umidi::protocol {
   
   // =============================================================================
   // Transport Types
   // =============================================================================
   
   enum class TransportType : uint8_t {
       SYSEX,      // USB MIDI SysEx (7-bit encoding) - WebMIDI compatible
       BULK,       // USB Bulk (8-bit raw) - WebUSB
       SYSEX8,     // MIDI 2.0 SysEx8 (8-bit, future)
   };
   
   // =============================================================================
   // Transport Capabilities
   // =============================================================================
   
   struct TransportCapabilities {
       uint32_t max_packet_size;      // Maximum single packet size
       uint32_t max_message_size;     // Maximum message size (may span packets)
       bool supports_8bit;            // True for Bulk/SysEx8, false for SysEx7
       bool supports_streaming;       // True if can stream without framing
       bool requires_encoding;        // True if needs 7-bit encoding
   };
   
   // Default capabilities
   inline constexpr TransportCapabilities SYSEX_CAPABILITIES = {
       .max_packet_size = 64,         // Typical USB-MIDI packet
       .max_message_size = 65536,     // Practical SysEx limit
       .supports_8bit = false,
       .supports_streaming = false,
       .requires_encoding = true,
   };
   
   inline constexpr TransportCapabilities BULK_CAPABILITIES = {
       .max_packet_size = 512,        // USB HS bulk
       .max_message_size = 1048576,   // 1MB practical limit
       .supports_8bit = true,
       .supports_streaming = true,
       .requires_encoding = false,
   };
   
   // =============================================================================
   // Transport Interface (Abstract Base)
   // =============================================================================
   
   class Transport {
   public:
       virtual ~Transport() = default;
   
       [[nodiscard]] virtual TransportType type() const noexcept = 0;
   
       [[nodiscard]] virtual const TransportCapabilities& capabilities() const noexcept = 0;
   
       [[nodiscard]] virtual bool is_connected() const noexcept = 0;
   
       virtual size_t send(const uint8_t* data, size_t len) = 0;
   
       virtual size_t receive(uint8_t* out, size_t max_len) = 0;
   
       virtual void flush() = 0;
   
       virtual void reset() = 0;
   };
   
   // =============================================================================
   // Bulk Framing (for WebUSB)
   // =============================================================================
   // Simple length-prefixed framing for 8-bit bulk transfers
   //
   // Frame format:
   // [LEN_HI:8][LEN_LO:8][DATA:LEN bytes][CRC16:16]
   //
   // Maximum frame size: 65535 bytes
   
   namespace bulk {
   
   constexpr size_t FRAME_HEADER_SIZE = 2;   // Length prefix
   constexpr size_t FRAME_TRAILER_SIZE = 2;  // CRC16
   constexpr size_t FRAME_OVERHEAD = FRAME_HEADER_SIZE + FRAME_TRAILER_SIZE;
   constexpr size_t MAX_FRAME_DATA = 65535 - FRAME_OVERHEAD;
   
   [[nodiscard]] inline constexpr uint16_t crc16(const uint8_t* data, size_t len) noexcept {
       uint16_t crc = 0xFFFF;
       for (size_t i = 0; i < len; ++i) {
           crc ^= uint16_t(data[i]) << 8;
           for (int j = 0; j < 8; ++j) {
               if (crc & 0x8000) {
                   crc = (crc << 1) ^ 0x1021;
               } else {
                   crc <<= 1;
               }
           }
       }
       return crc;
   }
   
   [[nodiscard]] inline constexpr size_t encode_frame(
       const uint8_t* data, size_t len, uint8_t* out) noexcept
   {
       if (len > MAX_FRAME_DATA) return 0;
   
       // Length prefix (big endian)
       out[0] = uint8_t((len >> 8) & 0xFF);
       out[1] = uint8_t(len & 0xFF);
   
       // Data
       for (size_t i = 0; i < len; ++i) {
           out[2 + i] = data[i];
       }
   
       // CRC16
       uint16_t crc = crc16(data, len);
       out[2 + len] = uint8_t((crc >> 8) & 0xFF);
       out[2 + len + 1] = uint8_t(crc & 0xFF);
   
       return len + FRAME_OVERHEAD;
   }
   
   [[nodiscard]] inline constexpr size_t decode_frame(
       const uint8_t* frame, size_t frame_len, uint8_t* out, size_t max_len) noexcept
   {
       if (frame_len < FRAME_OVERHEAD) return 0;
   
       // Extract length
       size_t data_len = (size_t(frame[0]) << 8) | frame[1];
       if (data_len + FRAME_OVERHEAD != frame_len) return 0;
       if (data_len > max_len) return 0;
   
       // Verify CRC
       uint16_t expected_crc = (uint16_t(frame[2 + data_len]) << 8) | frame[2 + data_len + 1];
       uint16_t actual_crc = crc16(&frame[2], data_len);
       if (expected_crc != actual_crc) return 0;
   
       // Copy data
       for (size_t i = 0; i < data_len; ++i) {
           out[i] = frame[2 + i];
       }
   
       return data_len;
   }
   
   } // namespace bulk
   
   // =============================================================================
   // SysEx Transport Implementation
   // =============================================================================
   // Wraps UMI SysEx protocol for WebMIDI compatibility
   
   template <size_t TxBufSize = 512, size_t RxBufSize = 512>
   class SysExTransport : public Transport {
   public:
       using SendFn = void (*)(const uint8_t* data, size_t len, void* ctx);
       using ReceiveFn = size_t (*)(uint8_t* out, size_t max_len, void* ctx);
   
       SysExTransport() = default;
   
       void init(SendFn send_fn, ReceiveFn recv_fn, void* ctx) noexcept {
           send_fn_ = send_fn;
           recv_fn_ = recv_fn;
           ctx_ = ctx;
           connected_ = true;
       }
   
       [[nodiscard]] TransportType type() const noexcept override {
           return TransportType::SYSEX;
       }
   
       [[nodiscard]] const TransportCapabilities& capabilities() const noexcept override {
           return SYSEX_CAPABILITIES;
       }
   
       [[nodiscard]] bool is_connected() const noexcept override {
           return connected_ && send_fn_ != nullptr;
       }
   
       size_t send(const uint8_t* data, size_t len) override {
           if (!is_connected()) return 0;
   
           // Build UMI SysEx message with 7-bit encoding
           MessageBuilder<TxBufSize> builder;
   
           // Use raw command for transport-level messages
           builder.begin(Command::STDIN_DATA, tx_seq_);
           builder.add_data(data, len);
           size_t msg_len = builder.finalize();
   
           send_fn_(builder.data(), msg_len, ctx_);
           tx_seq_ = (tx_seq_ + 1) & 0x7F;
   
           return len;
       }
   
       size_t receive(uint8_t* out, size_t max_len) override {
           if (!is_connected() || recv_fn_ == nullptr) return 0;
   
           // Receive raw SysEx
           uint8_t sysex_buf[RxBufSize];
           size_t sysex_len = recv_fn_(sysex_buf, sizeof(sysex_buf), ctx_);
           if (sysex_len == 0) return 0;
   
           // Parse UMI message
           auto msg = parse_message(sysex_buf, sysex_len);
           if (!msg.valid) return 0;
   
           // Decode payload
           return msg.decode_payload(out, max_len);
       }
   
       void flush() override {
           // SysEx messages are self-contained, no buffering
       }
   
       void reset() override {
           tx_seq_ = 0;
           rx_seq_ = 0;
       }
   
       void set_connected(bool connected) noexcept {
           connected_ = connected;
       }
   
   private:
       SendFn send_fn_ = nullptr;
       ReceiveFn recv_fn_ = nullptr;
       void* ctx_ = nullptr;
       uint8_t tx_seq_ = 0;
       uint8_t rx_seq_ = 0;
       bool connected_ = false;
   };
   
   // =============================================================================
   // Bulk Transport Implementation
   // =============================================================================
   // For WebUSB with length-prefixed framing
   
   template <size_t TxBufSize = 1024, size_t RxBufSize = 1024>
   class BulkTransport : public Transport {
   public:
       using SendFn = void (*)(const uint8_t* data, size_t len, void* ctx);
       using ReceiveFn = size_t (*)(uint8_t* out, size_t max_len, void* ctx);
   
       BulkTransport() = default;
   
       void init(SendFn send_fn, ReceiveFn recv_fn, void* ctx) noexcept {
           send_fn_ = send_fn;
           recv_fn_ = recv_fn;
           ctx_ = ctx;
           connected_ = true;
       }
   
       [[nodiscard]] TransportType type() const noexcept override {
           return TransportType::BULK;
       }
   
       [[nodiscard]] const TransportCapabilities& capabilities() const noexcept override {
           return BULK_CAPABILITIES;
       }
   
       [[nodiscard]] bool is_connected() const noexcept override {
           return connected_ && send_fn_ != nullptr;
       }
   
       size_t send(const uint8_t* data, size_t len) override {
           if (!is_connected()) return 0;
           if (len > bulk::MAX_FRAME_DATA) return 0;
   
           // Encode with bulk framing
           uint8_t frame[TxBufSize];
           size_t frame_len = bulk::encode_frame(data, len, frame);
           if (frame_len == 0) return 0;
   
           send_fn_(frame, frame_len, ctx_);
           return len;
       }
   
       size_t receive(uint8_t* out, size_t max_len) override {
           if (!is_connected() || recv_fn_ == nullptr) return 0;
   
           // Receive raw frame
           uint8_t frame[RxBufSize];
           size_t frame_len = recv_fn_(frame, sizeof(frame), ctx_);
           if (frame_len == 0) return 0;
   
           // Decode frame
           return bulk::decode_frame(frame, frame_len, out, max_len);
       }
   
       void flush() override {
           // Bulk transfers may need flushing on some platforms
       }
   
       void reset() override {
           // Reset any pending state
       }
   
       void set_connected(bool connected) noexcept {
           connected_ = connected;
       }
   
   private:
       SendFn send_fn_ = nullptr;
       ReceiveFn recv_fn_ = nullptr;
       void* ctx_ = nullptr;
       bool connected_ = false;
   };
   
   // =============================================================================
   // Transport-Agnostic Message Handler
   // =============================================================================
   // Wraps protocol handlers to work with any transport
   
   template <typename ProtocolHandler>
   class TransportHandler {
   public:
       TransportHandler(Transport& transport, ProtocolHandler& handler)
           : transport_(transport), handler_(handler) {}
   
       bool process(uint32_t current_time) {
           uint8_t buffer[1024];
           size_t len = transport_.receive(buffer, sizeof(buffer));
           if (len == 0) return false;
   
           // Dispatch to protocol handler
           return handler_.process(buffer, len, current_time,
               [this](const uint8_t* data, size_t len) {
                   transport_.send(data, len);
               });
       }
   
       size_t send(const uint8_t* data, size_t len) {
           return transport_.send(data, len);
       }
   
       Transport& transport() { return transport_; }
       ProtocolHandler& handler() { return handler_; }
   
   private:
       Transport& transport_;
       ProtocolHandler& handler_;
   };
   
   // =============================================================================
   // Multi-Transport Manager
   // =============================================================================
   // Manages multiple transports with automatic fallback
   
   template <size_t MaxTransports = 2>
   class TransportManager {
   public:
       bool register_transport(Transport* transport) noexcept {
           if (count_ >= MaxTransports) return false;
           transports_[count_++] = transport;
           return true;
       }
   
       [[nodiscard]] Transport* get_best() noexcept {
           Transport* best = nullptr;
           for (size_t i = 0; i < count_; ++i) {
               if (transports_[i] && transports_[i]->is_connected()) {
                   if (!best || transports_[i]->type() == TransportType::BULK) {
                       best = transports_[i];
                   }
               }
           }
           return best;
       }
   
       [[nodiscard]] Transport* get(TransportType type) noexcept {
           for (size_t i = 0; i < count_; ++i) {
               if (transports_[i] && transports_[i]->type() == type) {
                   return transports_[i];
               }
           }
           return nullptr;
       }
   
       [[nodiscard]] bool any_connected() const noexcept {
           for (size_t i = 0; i < count_; ++i) {
               if (transports_[i] && transports_[i]->is_connected()) {
                   return true;
               }
           }
           return false;
       }
   
       [[nodiscard]] size_t count() const noexcept { return count_; }
   
   private:
       Transport* transports_[MaxTransports] = {};
       size_t count_ = 0;
   };
   
   } // namespace umidi::protocol
