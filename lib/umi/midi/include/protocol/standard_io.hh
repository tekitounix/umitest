// SPDX-License-Identifier: MIT
/// @file standard_io.hh
/// @brief Standard IO (stdin/stdout/stderr) over MIDI SysEx
#pragma once

#include "message.hh"
#include <cstdint>
#include <cstddef>

namespace umidi::protocol {

/// Callback for received stdin data
using StdinCallback = void (*)(const uint8_t* data, size_t len, void* ctx);

/// @brief Standard IO over SysEx.
/// @tparam TxBufSize Transmit buffer size
/// @tparam RxBufSize Receive buffer size
template <size_t TxBufSize = 512, size_t RxBufSize = 512>
class StandardIO {
public:
    /// @brief Set callback for incoming stdin data.
    void set_stdin_callback(StdinCallback cb, void* ctx) noexcept {
        stdin_cb_ = cb;
        stdin_ctx_ = ctx;
    }

    /// @brief Write to stdout.
    template <typename SendFn>
    size_t write_stdout(const uint8_t* data, size_t len, SendFn send_fn) {
        return write_stream(Command::STDOUT_DATA, data, len, send_fn);
    }

    /// @brief Write to stderr.
    template <typename SendFn>
    size_t write_stderr(const uint8_t* data, size_t len, SendFn send_fn) {
        return write_stream(Command::STDERR_DATA, data, len, send_fn);
    }

    /// @brief Process incoming SysEx message.
    /// @return true if message was handled
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

    /// @brief Reset IO state.
    void reset() noexcept {
        tx_seq_ = 0;
        rx_seq_ = 0;
        tx_paused_ = false;
        eof_received_ = false;
    }

    /// @brief Check if transmission is paused (flow control).
    [[nodiscard]] bool is_paused() const noexcept { return tx_paused_; }

    /// @brief Check if EOF was received.
    [[nodiscard]] bool eof() const noexcept { return eof_received_; }

private:
    template <typename SendFn>
    size_t write_stream(Command cmd, const uint8_t* data, size_t len,
                        SendFn send_fn) {
        if (tx_paused_) return 0;
        // Large chunk for shell output - send entire response in one SysEx if possible
        // USB SysEx can span multiple USB packets (64 bytes each)
        constexpr size_t MAX_CHUNK = 1024;

        size_t sent = 0;
        while (sent < len) {
            size_t chunk = (len - sent > MAX_CHUNK) ? MAX_CHUNK : (len - sent);
            MessageBuilder<1280> builder;  // Enough for 1024-byte chunks after 7-bit encoding (~8/7 ratio + overhead)
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
