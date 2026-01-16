
.. _program_listing_file_protocol_standard_io.hh:

Program Listing for File standard_io.hh
=======================================

|exhale_lsh| :ref:`Return to documentation for file <file_protocol_standard_io.hh>` (``protocol/standard_io.hh``)

.. |exhale_lsh| unicode:: U+021B0 .. UPWARDS ARROW WITH TIP LEFTWARDS

.. code-block:: cpp

   // SPDX-License-Identifier: MIT
   #pragma once
   
   #include "message.hh"
   #include <cstdint>
   #include <cstddef>
   
   namespace umidi::protocol {
   
   using StdinCallback = void (*)(const uint8_t* data, size_t len, void* ctx);
   
   template <size_t TxBufSize = 512, size_t RxBufSize = 512>
   class StandardIO {
   public:
       void set_stdin_callback(StdinCallback cb, void* ctx) noexcept {
           stdin_cb_ = cb;
           stdin_ctx_ = ctx;
       }
   
       template <typename SendFn>
       size_t write_stdout(const uint8_t* data, size_t len, SendFn send_fn) {
           return write_stream(Command::STDOUT_DATA, data, len, send_fn);
       }
   
       template <typename SendFn>
       size_t write_stderr(const uint8_t* data, size_t len, SendFn send_fn) {
           return write_stream(Command::STDERR_DATA, data, len, send_fn);
       }
   
       bool process_message(const uint8_t* data, size_t len) {
           auto msg = parse_message(data, len);
           if (!msg.valid) return false;
   
           switch (msg.command) {
           case Command::STDIN_DATA: {
               if (stdin_cb_) {
                   uint8_t decoded[decoded_size(RxBufSize)];
                   size_t dec_len = msg.decode_payload(decoded, sizeof(decoded));
                   if (dec_len > 0) {
                       stdin_cb_(decoded, dec_len, stdin_ctx_);
                   }
               }
               rx_seq_ = (msg.sequence + 1) & 0x7F;
               return true;
           }
           case Command::STDIN_EOF:
               eof_received_ = true;
               return true;
           case Command::FLOW_CTRL:
               if (msg.payload_len >= 1) {
                   tx_paused_ = (msg.payload[0] == static_cast<uint8_t>(FlowControl::XOFF));
               }
               return true;
           case Command::RESET:
               reset();
               return true;
           default:
               return false;
           }
       }
   
       void reset() noexcept {
           tx_seq_ = 0;
           rx_seq_ = 0;
           tx_paused_ = false;
           eof_received_ = false;
       }
   
       [[nodiscard]] bool is_paused() const noexcept { return tx_paused_; }
   
       [[nodiscard]] bool eof() const noexcept { return eof_received_; }
   
   private:
       template <typename SendFn>
       size_t write_stream(Command cmd, const uint8_t* data, size_t len,
                           SendFn send_fn) {
           if (tx_paused_) return 0;
           constexpr size_t MAX_CHUNK = 48;
   
           size_t sent = 0;
           while (sent < len) {
               size_t chunk = (len - sent > MAX_CHUNK) ? MAX_CHUNK : (len - sent);
               MessageBuilder<64> builder;
               builder.begin(cmd, tx_seq_);
               builder.add_data(data + sent, chunk);
               send_fn(builder.data(), builder.finalize());
               tx_seq_ = (tx_seq_ + 1) & 0x7F;
               sent += chunk;
           }
           return sent;
       }
   
       StdinCallback stdin_cb_ = nullptr;
       void* stdin_ctx_ = nullptr;
       uint8_t tx_seq_ = 0;
       uint8_t rx_seq_ = 0;
       bool tx_paused_ = false;
       bool eof_received_ = false;
   };
   
   } // namespace umidi::protocol
