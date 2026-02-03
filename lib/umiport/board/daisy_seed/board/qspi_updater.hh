// SPDX-License-Identifier: MIT
// QSPI Firmware Updater for Daisy (DFU over SysEx)
// Handles FW_BEGIN/FW_DATA/FW_VERIFY/FW_COMMIT for QSPI flash
// Self-contained implementation without external protocol dependencies
#pragma once

#include <cstdint>
#include <cstring>

#include "qspi_program.hh"

namespace umi::daisy {

/// DFU Commands
namespace dfu {
constexpr std::uint8_t FW_QUERY = 0x10;
constexpr std::uint8_t FW_INFO = 0x11;
constexpr std::uint8_t FW_BEGIN = 0x12;
constexpr std::uint8_t FW_DATA = 0x13;
constexpr std::uint8_t FW_VERIFY = 0x14;
constexpr std::uint8_t FW_COMMIT = 0x15;
constexpr std::uint8_t FW_REBOOT = 0x17;
constexpr std::uint8_t FW_ACK = 0x18;
constexpr std::uint8_t FW_READ_CRC = 0x19;   // Read CRC from QSPI flash
constexpr std::uint8_t FW_READ_DATA = 0x1A;  // Read raw data from QSPI flash (debug)
constexpr std::uint8_t FW_DEBUG_QSPI = 0x1B; // Dump QSPI registers (debug)
constexpr std::uint8_t FW_FLASH_STATUS = 0x1C; // Read flash Status Register directly
constexpr std::uint8_t FW_JEDEC_ID = 0x1D;     // Read JEDEC ID (Manufacturer/Device ID)
constexpr std::uint8_t FW_WRITE_TEST = 0x1E;   // Write-then-read test (debug)
// UMI SysEx ID
constexpr std::uint8_t UMI_SYSEX_ID[] = {0x7E, 0x7F, 0x00};
constexpr std::size_t UMI_SYSEX_ID_LEN = 3;
} // namespace dfu

/// Update target type
enum class UpdateTarget : std::uint8_t {
    KERNEL = 0x00, // Internal flash (not supported here)
    APP = 0x01,    // QSPI external flash for .umia app
};

/// Update state
enum class QspiUpdateState : std::uint8_t {
    IDLE,            // No update in progress
    ERASING,         // Erasing sectors
    RECEIVING,       // Receiving data chunks
    VERIFYING,       // Verifying CRC
    READY_TO_COMMIT, // Verified, ready to commit
    ERROR,           // Error state
};

/// QSPI Firmware Updater
/// Handles DFU protocol for external QSPI flash (app binary)
class QspiUpdater {
  public:
    static constexpr std::uint32_t QSPI_APP_BASE = 0x90000000;
    static constexpr std::uint32_t QSPI_APP_MAX_SIZE = 8 * 1024 * 1024; // 8MB
    // CHUNK_SIZE must be small enough that 7-bit encoded data + SysEx header
    // fits in MidiProcessor's sysex_buf_ (256 bytes).
    // 175 bytes raw -> ~200 bytes encoded + ~7 bytes header = ~207 bytes < 256
    static constexpr std::uint32_t CHUNK_SIZE = 175;

    using SendFn = void (*)(const std::uint8_t* data, std::uint16_t len);
    using PollFn = void (*)(); // USB polling callback

    QspiUpdater() = default;

    /// Set USB polling callback (called during long operations like erase)
    void set_poll_callback(PollFn poll_fn) noexcept { poll_fn_ = poll_fn; }

    /// Process incoming SysEx message
    /// @return true if message was handled
    bool process_message(const std::uint8_t* data, std::size_t len, SendFn send_fn) {
        // Minimum: F0 + ID(3) + CMD + SEQ + CHECKSUM + F7 = 8
        if (len < 8)
            return false;
        if (data[0] != 0xF0 || data[len - 1] != 0xF7)
            return false;

        // Check UMI SysEx ID
        for (std::size_t i = 0; i < dfu::UMI_SYSEX_ID_LEN; ++i) {
            if (data[1 + i] != dfu::UMI_SYSEX_ID[i])
                return false;
        }

        std::size_t cmd_pos = 1 + dfu::UMI_SYSEX_ID_LEN;
        std::size_t checksum_pos = len - 2;

        // Verify checksum
        std::uint8_t expected = data[checksum_pos];
        std::uint8_t actual = calculate_checksum(&data[cmd_pos], checksum_pos - cmd_pos);
        if (expected != actual)
            return false;

        std::uint8_t cmd = data[cmd_pos];
        std::uint8_t seq = data[cmd_pos + 1];
        const std::uint8_t* payload = &data[cmd_pos + 2];
        std::size_t payload_len = checksum_pos - cmd_pos - 2;

        switch (cmd) {
        case dfu::FW_QUERY:
            return handle_query(seq, send_fn);
        case dfu::FW_BEGIN:
            return handle_begin(seq, payload, payload_len, send_fn);
        case dfu::FW_DATA:
            return handle_data(seq, payload, payload_len, send_fn);
        case dfu::FW_VERIFY:
            return handle_verify(seq, payload, payload_len, send_fn);
        case dfu::FW_COMMIT:
            return handle_commit(seq, send_fn);
        case dfu::FW_REBOOT:
            return handle_reboot(seq, send_fn);
        case dfu::FW_READ_CRC:
            return handle_read_crc(seq, payload, payload_len, send_fn);
        case dfu::FW_READ_DATA:
            return handle_read_data(seq, payload, payload_len, send_fn);
        case dfu::FW_DEBUG_QSPI:
            return handle_debug_qspi(seq, send_fn);
        case dfu::FW_FLASH_STATUS:
            return handle_flash_status(seq, send_fn);
        case dfu::FW_JEDEC_ID:
            return handle_jedec_id(seq, send_fn);
        case dfu::FW_WRITE_TEST:
            return handle_write_test(seq, send_fn);
        default:
            return false;
        }
    }

    [[nodiscard]] QspiUpdateState state() const noexcept { return state_; }
    [[nodiscard]] std::uint32_t received_bytes() const noexcept { return received_bytes_; }
    [[nodiscard]] std::uint32_t total_size() const noexcept { return expected_size_; }
    [[nodiscard]] std::uint8_t progress_percent() const noexcept {
        if (expected_size_ == 0)
            return 0;
        return static_cast<std::uint8_t>((received_bytes_ * 100) / expected_size_);
    }

    void reset() noexcept {
        state_ = QspiUpdateState::IDLE;
        expected_size_ = 0;
        received_bytes_ = 0;
        running_crc_ = 0xFFFFFFFF;
        expected_seq_ = 0;
    }

  private:
    // 7-bit decode helper
    static std::size_t decode_7bit(const std::uint8_t* in, std::size_t in_len, std::uint8_t* out) {
        std::size_t out_pos = 0;
        std::size_t in_pos = 0;
        while (in_pos < in_len) {
            std::uint8_t msb_byte = in[in_pos++];
            if (msb_byte > 0x7F)
                return 0;
            std::size_t remaining = in_len - in_pos;
            std::size_t chunk_len = (remaining < 7) ? remaining : 7;
            for (std::size_t j = 0; j < chunk_len; ++j) {
                if (in_pos >= in_len)
                    break;
                std::uint8_t byte = in[in_pos++];
                if (byte > 0x7F)
                    return 0;
                if (msb_byte & (1 << j)) {
                    byte |= 0x80;
                }
                out[out_pos++] = byte;
            }
        }
        return out_pos;
    }

    // 7-bit encode helper
    static std::size_t encode_7bit(const std::uint8_t* in, std::size_t in_len, std::uint8_t* out) {
        std::size_t out_pos = 0;
        for (std::size_t i = 0; i < in_len; i += 7) {
            std::uint8_t msb_byte = 0;
            std::size_t chunk_len = (in_len - i < 7) ? (in_len - i) : 7;
            for (std::size_t j = 0; j < chunk_len; ++j) {
                if (in[i + j] & 0x80) {
                    msb_byte |= (1 << j);
                }
            }
            out[out_pos++] = msb_byte;
            for (std::size_t j = 0; j < chunk_len; ++j) {
                out[out_pos++] = in[i + j] & 0x7F;
            }
        }
        return out_pos;
    }

    static std::uint8_t calculate_checksum(const std::uint8_t* data, std::size_t len) {
        std::uint8_t checksum = 0;
        for (std::size_t i = 0; i < len; ++i) {
            checksum ^= data[i];
        }
        return checksum & 0x7F;
    }

    // CRC-32 (IEEE 802.3)
    static std::uint32_t crc32_update(std::uint32_t crc, const std::uint8_t* data, std::size_t len) {
        static constexpr std::uint32_t crc_table[16] = {
            0x00000000,
            0x1DB71064,
            0x3B6E20C8,
            0x26D930AC,
            0x76DC4190,
            0x6B6B51F4,
            0x4DB26158,
            0x5005713C,
            0xEDB88320,
            0xF00F9344,
            0xD6D6A3E8,
            0xCB61B38C,
            0x9B64C2B0,
            0x86D3D2D4,
            0xA00AE278,
            0xBDBDF21C,
        };
        for (std::size_t i = 0; i < len; ++i) {
            crc = crc_table[(crc ^ data[i]) & 0x0F] ^ (crc >> 4);
            crc = crc_table[(crc ^ (data[i] >> 4)) & 0x0F] ^ (crc >> 4);
        }
        return crc;
    }

    void update_crc(const std::uint8_t* data, std::size_t len) { running_crc_ = crc32_update(running_crc_, data, len); }

    // Build and send a SysEx response
    void send_response(
        std::uint8_t cmd, std::uint8_t seq, const std::uint8_t* payload, std::size_t payload_len, SendFn send_fn) {
        std::uint8_t buf[64];
        std::size_t pos = 0;

        buf[pos++] = 0xF0;
        for (std::size_t i = 0; i < dfu::UMI_SYSEX_ID_LEN; ++i) {
            buf[pos++] = dfu::UMI_SYSEX_ID[i];
        }
        std::size_t cmd_start = pos;
        buf[pos++] = cmd;
        buf[pos++] = seq & 0x7F;

        if (payload && payload_len > 0) {
            pos += encode_7bit(payload, payload_len, &buf[pos]);
        }

        buf[pos] = calculate_checksum(&buf[cmd_start], pos - cmd_start);
        pos++;
        buf[pos++] = 0xF7;

        send_fn(buf, static_cast<std::uint16_t>(pos));
    }

    void send_ack(std::uint8_t seq, std::uint8_t status, SendFn send_fn) {
        std::uint8_t payload[1] = {status};
        send_response(dfu::FW_ACK, seq, payload, 1, send_fn);
    }

    bool handle_query(std::uint8_t seq, SendFn send_fn) {
        // Device info: version, QSPI size, base address
        std::uint8_t payload[12] = {
            0x01,
            0x00,
            0x00,
            0x00, // Version 1.0.0.0
            static_cast<std::uint8_t>(QSPI_APP_MAX_SIZE >> 24),
            static_cast<std::uint8_t>((QSPI_APP_MAX_SIZE >> 16) & 0xFF),
            static_cast<std::uint8_t>((QSPI_APP_MAX_SIZE >> 8) & 0xFF),
            static_cast<std::uint8_t>(QSPI_APP_MAX_SIZE & 0xFF),
            static_cast<std::uint8_t>(QSPI_APP_BASE >> 24),
            static_cast<std::uint8_t>((QSPI_APP_BASE >> 16) & 0xFF),
            static_cast<std::uint8_t>((QSPI_APP_BASE >> 8) & 0xFF),
            static_cast<std::uint8_t>(QSPI_APP_BASE & 0xFF),
        };
        send_response(dfu::FW_INFO, seq, payload, sizeof(payload), send_fn);
        return true;
    }

    bool handle_begin(std::uint8_t seq, const std::uint8_t* payload, std::size_t payload_len, SendFn send_fn) {
        // Allow new update from IDLE or ERROR state (recovery after error)
        if (state_ != QspiUpdateState::IDLE && state_ != QspiUpdateState::ERROR) {
            send_ack(seq, 0x01, send_fn); // UPDATE_IN_PROGRESS
            return true;
        }

        // Decode payload
        std::uint8_t decoded[16];
        std::size_t dec_len = decode_7bit(payload, payload_len, decoded);
        if (dec_len < 5) {
            send_ack(seq, 0x02, send_fn); // INVALID_COMMAND
            return true;
        }

        // Payload: [target(1)] [size(4)]
        auto target = static_cast<UpdateTarget>(decoded[0]);
        if (target != UpdateTarget::APP) {
            send_ack(seq, 0x03, send_fn); // UNSUPPORTED_TARGET
            return true;
        }

        expected_size_ = (std::uint32_t(decoded[1]) << 24) | (std::uint32_t(decoded[2]) << 16) |
                         (std::uint32_t(decoded[3]) << 8) | decoded[4];

        if (expected_size_ > QSPI_APP_MAX_SIZE) {
            send_ack(seq, 0x04, send_fn); // SIZE_TOO_LARGE
            return true;
        }

        // Send ACK immediately before starting erase (erase can take seconds)
        // This allows the host to know we received the command
        state_ = QspiUpdateState::ERASING;
        received_bytes_ = 0;
        running_crc_ = 0xFFFFFFFF;
        expected_seq_ = (seq + 1) & 0x7F;

        send_ack(seq, 0x00, send_fn); // OK - erase starting

        // Perform erase with USB polling between sectors
        // This allows USB to stay responsive during the long erase operation
        auto poll = [this]() {
            if (poll_fn_) {
                poll_fn_();
            }
        };

        if (!qspi_erase_range_with_poll(QSPI_APP_BASE, expected_size_, poll)) {
            state_ = QspiUpdateState::ERROR;
            // Note: Already sent ACK, host will get error on first FW_DATA
            return true;
        }

        state_ = QspiUpdateState::RECEIVING;
        return true;
    }

    bool handle_data(std::uint8_t seq, const std::uint8_t* payload, std::size_t payload_len, SendFn send_fn) {
        // Check if still erasing (async erase not yet complete)
        if (state_ == QspiUpdateState::ERASING) {
            send_ack(seq, 0x0C, send_fn); // ERASE_IN_PROGRESS - retry later
            return true;
        }

        if (state_ != QspiUpdateState::RECEIVING) {
            send_ack(seq, 0x06, send_fn); // UPDATE_NOT_STARTED
            return true;
        }

        if (seq != expected_seq_) {
            send_ack(seq, 0x07, send_fn); // INVALID_SEQUENCE
            return true;
        }

        // Decode payload
        std::uint8_t decoded[CHUNK_SIZE + 16];
        std::size_t dec_len = decode_7bit(payload, payload_len, decoded);
        if (dec_len == 0) {
            send_ack(seq, 0x02, send_fn); // INVALID_COMMAND
            return true;
        }

        // Write to QSPI
        std::uint32_t write_addr = QSPI_APP_BASE + received_bytes_;
        if (!qspi_program(write_addr, decoded, dec_len)) {
            state_ = QspiUpdateState::ERROR;
            send_ack(seq, 0x08, send_fn); // FLASH_ERROR
            return true;
        }

        update_crc(decoded, dec_len);
        received_bytes_ += dec_len;
        expected_seq_ = (expected_seq_ + 1) & 0x7F;

        send_ack(seq, 0x00, send_fn); // OK
        return true;
    }

    bool handle_verify(std::uint8_t seq, const std::uint8_t* payload, std::size_t payload_len, SendFn send_fn) {
        if (state_ != QspiUpdateState::RECEIVING) {
            send_ack(seq, 0x06, send_fn); // UPDATE_NOT_STARTED
            return true;
        }

        if (received_bytes_ != expected_size_) {
            send_ack(seq, 0x09, send_fn); // SIZE_MISMATCH
            return true;
        }

        std::uint8_t decoded[16];
        std::size_t dec_len = decode_7bit(payload, payload_len, decoded);
        if (dec_len < 4) {
            send_ack(seq, 0x02, send_fn); // INVALID_COMMAND
            return true;
        }

        std::uint32_t expected_crc = (std::uint32_t(decoded[0]) << 24) | (std::uint32_t(decoded[1]) << 16) |
                                     (std::uint32_t(decoded[2]) << 8) | decoded[3];

        std::uint32_t final_crc = running_crc_ ^ 0xFFFFFFFF;
        if (final_crc != expected_crc) {
            state_ = QspiUpdateState::ERROR;
            send_ack(seq, 0x0A, send_fn); // VERIFY_FAILED
            return true;
        }

        state_ = QspiUpdateState::READY_TO_COMMIT;
        send_ack(seq, 0x00, send_fn); // OK
        return true;
    }

    bool handle_commit(std::uint8_t seq, SendFn send_fn) {
        if (state_ != QspiUpdateState::READY_TO_COMMIT) {
            send_ack(seq, 0x0B, send_fn); // NOT_READY
            return true;
        }

        // For QSPI, commit is essentially a no-op — data is already written
        state_ = QspiUpdateState::IDLE;
        send_ack(seq, 0x00, send_fn); // OK
        return true;
    }

    bool handle_reboot(std::uint8_t seq, SendFn send_fn) {
        send_ack(seq, 0x00, send_fn); // OK

        // Wait a bit for ACK to be sent
        for (int i = 0; i < 100000; ++i) {
            asm volatile("nop");
        }

        // NVIC_SystemReset
        auto* AIRCR = reinterpret_cast<volatile std::uint32_t*>(0xE000ED0C);
        *AIRCR = (0x5FA << 16) | (1 << 2); // SYSRESETREQ

        while (true) {
        } // Wait for reset
    }

    /// Handle FW_READ_CRC: Read data from QSPI and compute CRC
    /// Payload: [offset(4)][size(4)] - big endian
    /// Response: [status(1)][crc32(4)] - big endian
    bool handle_read_crc(std::uint8_t seq, const std::uint8_t* payload, std::size_t payload_len, SendFn send_fn) {
        std::uint8_t decoded[16];
        std::size_t dec_len = decode_7bit(payload, payload_len, decoded);
        if (dec_len < 8) {
            send_ack(seq, 0x02, send_fn); // INVALID_COMMAND
            return true;
        }

        std::uint32_t offset = (std::uint32_t(decoded[0]) << 24) | (std::uint32_t(decoded[1]) << 16) |
                               (std::uint32_t(decoded[2]) << 8) | decoded[3];
        std::uint32_t size = (std::uint32_t(decoded[4]) << 24) | (std::uint32_t(decoded[5]) << 16) |
                             (std::uint32_t(decoded[6]) << 8) | decoded[7];

        // Validate range
        if (offset + size > QSPI_APP_MAX_SIZE) {
            send_ack(seq, 0x04, send_fn); // SIZE_TOO_LARGE
            return true;
        }

        // Re-enter memory-mapped mode to ensure fresh state after any writes
        qspi_enter_memory_mapped();

        // Read from QSPI and compute CRC
        // QSPI is memory-mapped at QSPI_APP_BASE
        const auto* qspi_data = reinterpret_cast<const std::uint8_t*>(QSPI_APP_BASE + offset);
        std::uint32_t crc = 0xFFFFFFFF;
        crc = crc32_update(crc, qspi_data, size);
        crc ^= 0xFFFFFFFF;

        // Response: [status(1)][crc32(4)]
        std::uint8_t response[5] = {
            0x00, // OK
            static_cast<std::uint8_t>((crc >> 24) & 0xFF),
            static_cast<std::uint8_t>((crc >> 16) & 0xFF),
            static_cast<std::uint8_t>((crc >> 8) & 0xFF),
            static_cast<std::uint8_t>(crc & 0xFF),
        };
        send_response(dfu::FW_ACK, seq, response, sizeof(response), send_fn);
        return true;
    }

    /// Handle FW_READ_DATA: Read raw bytes from QSPI (for debugging)
    /// Payload: [offset(4)][size(1)] - big endian, size max 32 bytes
    /// Response: [status(1)][data...]
    bool handle_read_data(std::uint8_t seq, const std::uint8_t* payload, std::size_t payload_len, SendFn send_fn) {
        std::uint8_t decoded[16];
        std::size_t dec_len = decode_7bit(payload, payload_len, decoded);
        if (dec_len < 5) {
            send_ack(seq, 0x02, send_fn); // INVALID_COMMAND
            return true;
        }

        std::uint32_t offset = (std::uint32_t(decoded[0]) << 24) | (std::uint32_t(decoded[1]) << 16) |
                               (std::uint32_t(decoded[2]) << 8) | decoded[3];
        std::uint8_t size = decoded[4];
        
        // Limit read size
        if (size > 32) size = 32;

        // Validate range
        if (offset + size > QSPI_APP_MAX_SIZE) {
            send_ack(seq, 0x04, send_fn); // SIZE_TOO_LARGE
            return true;
        }

        // Re-enter memory-mapped mode to ensure fresh state after any writes
        qspi_enter_memory_mapped();

        // Read from QSPI
        const auto* qspi_data = reinterpret_cast<const std::uint8_t*>(QSPI_APP_BASE + offset);
        
        // Response: [status(1)][data...]
        std::uint8_t response[33];
        response[0] = 0x00; // OK
        for (std::uint8_t i = 0; i < size; ++i) {
            response[1 + i] = qspi_data[i];
        }
        send_response(dfu::FW_ACK, seq, response, 1 + size, send_fn);
        return true;
    }

    /// Handle FW_DEBUG_QSPI: Dump QUADSPI registers for debugging
    /// Response: [status(1)][CR(4)][DCR(4)][SR(4)][CCR(4)][DLR(4)][AR(4)][ABR(4)]
    bool handle_debug_qspi(std::uint8_t seq, SendFn send_fn) {
        using namespace ::umi::stm32h7;
        mm::DirectTransportT<> t;

        auto cr = t.read(QUADSPI::CR{});
        auto dcr = t.read(QUADSPI::DCR{});
        auto sr = t.read(QUADSPI::SR{});
        auto ccr = t.read(QUADSPI::CCR{});
        auto dlr = t.read(QUADSPI::DLR{});
        auto ar = t.read(QUADSPI::AR{});
        auto abr = t.read(QUADSPI::ABR{});

        // Response: [status(1)][registers...]
        std::uint8_t response[29];
        response[0] = 0x00; // OK

        // CR (4 bytes, big endian)
        response[1] = static_cast<std::uint8_t>((cr >> 24) & 0xFF);
        response[2] = static_cast<std::uint8_t>((cr >> 16) & 0xFF);
        response[3] = static_cast<std::uint8_t>((cr >> 8) & 0xFF);
        response[4] = static_cast<std::uint8_t>(cr & 0xFF);

        // DCR
        response[5] = static_cast<std::uint8_t>((dcr >> 24) & 0xFF);
        response[6] = static_cast<std::uint8_t>((dcr >> 16) & 0xFF);
        response[7] = static_cast<std::uint8_t>((dcr >> 8) & 0xFF);
        response[8] = static_cast<std::uint8_t>(dcr & 0xFF);

        // SR
        response[9] = static_cast<std::uint8_t>((sr >> 24) & 0xFF);
        response[10] = static_cast<std::uint8_t>((sr >> 16) & 0xFF);
        response[11] = static_cast<std::uint8_t>((sr >> 8) & 0xFF);
        response[12] = static_cast<std::uint8_t>(sr & 0xFF);

        // CCR
        response[13] = static_cast<std::uint8_t>((ccr >> 24) & 0xFF);
        response[14] = static_cast<std::uint8_t>((ccr >> 16) & 0xFF);
        response[15] = static_cast<std::uint8_t>((ccr >> 8) & 0xFF);
        response[16] = static_cast<std::uint8_t>(ccr & 0xFF);

        // DLR
        response[17] = static_cast<std::uint8_t>((dlr >> 24) & 0xFF);
        response[18] = static_cast<std::uint8_t>((dlr >> 16) & 0xFF);
        response[19] = static_cast<std::uint8_t>((dlr >> 8) & 0xFF);
        response[20] = static_cast<std::uint8_t>(dlr & 0xFF);

        // AR
        response[21] = static_cast<std::uint8_t>((ar >> 24) & 0xFF);
        response[22] = static_cast<std::uint8_t>((ar >> 16) & 0xFF);
        response[23] = static_cast<std::uint8_t>((ar >> 8) & 0xFF);
        response[24] = static_cast<std::uint8_t>(ar & 0xFF);

        // ABR
        response[25] = static_cast<std::uint8_t>((abr >> 24) & 0xFF);
        response[26] = static_cast<std::uint8_t>((abr >> 16) & 0xFF);
        response[27] = static_cast<std::uint8_t>((abr >> 8) & 0xFF);
        response[28] = static_cast<std::uint8_t>(abr & 0xFF);

        send_response(dfu::FW_ACK, seq, response, sizeof(response), send_fn);
        return true;
    }

    /// Handle FW_FLASH_STATUS: Read flash Status Register and first 8 bytes via indirect read
    /// Response: [status(1)][flash_sr(1)][data0-7(8)]
    bool handle_flash_status(std::uint8_t seq, SendFn send_fn) {
        using namespace ::umi::stm32h7;
        mm::DirectTransportT<> t;
        
        // Exit memory-mapped mode to perform indirect read
        qspi_exit_memory_mapped();
        
        // Read flash Status Register
        std::uint8_t flash_sr = qspi_read_byte(is25lp::READ_STATUS);
        
        // Clear all flags and abort any pending operation before starting new read
        auto cr = t.read(QUADSPI::CR{});
        t.write(QUADSPI::CR::value(cr | (1U << 1))); // ABORT
        while (t.read(QUADSPI::CR{}) & (1U << 1)) {}
        t.write(QUADSPI::FCR::value(0x1F)); // Clear all flags
        
        // Now try to read first 8 bytes via indirect read (single line)
        std::uint8_t data[8] = {0};
        
        qspi_wait_busy();
        t.write(QUADSPI::DLR::value(7)); // 8 bytes - BEFORE CCR
        
        // Use simple single-line read (command 0x03)
        // IMPORTANT: CCR must be written BEFORE AR!
        t.write(QUADSPI::CCR::value(
            static_cast<std::uint32_t>(0x03) |  // READ command
            (qspi_mode::SINGLE << 8) |          // IMODE: single line
            (qspi_mode::SINGLE << 10) |         // ADMODE: single line
            (qspi_adsize::ADDR_24BIT << 12) |   // ADSIZE: 24-bit
            (qspi_mode::SINGLE << 24) |         // DMODE: single line
            (qspi_fmode::INDIRECT_READ << 26)
        ));
        
        // AR - set address AFTER CCR (this starts the address phase)
        t.write(QUADSPI::AR::value(0)); // Address 0
        
        // Wait for transfer complete with timeout
        std::uint32_t timeout = 1000000;
        while (!(t.read(QUADSPI::SR{}) & (1U << 1)) && timeout > 0) { 
            --timeout;
        }
        
        if (timeout > 0) {
            // Read data from FIFO
            auto* dr = reinterpret_cast<volatile std::uint8_t*>(0x52005020);
            for (int i = 0; i < 8; ++i) {
                data[i] = *dr;
            }
        }
        
        t.write(QUADSPI::FCR::value(1U << 1)); // Clear TCF
        
        // Re-enter memory-mapped mode WITHOUT calling qspi_wait_ready()
        // (flash should already be ready since we didn't write anything)
        qspi_wait_busy();
        t.write(QUADSPI::ABR::value(0x000000A0));
        t.write(QUADSPI::CCR::value(
            static_cast<std::uint32_t>(is25lp::QUAD_READ) | 
            (qspi_mode::SINGLE << 8) |   // IMODE
            (qspi_mode::QUAD << 10) |    // ADMODE
            (qspi_adsize::ADDR_24BIT << 12) | 
            (qspi_mode::QUAD << 14) |    // ABMODE
            (0U << 16) |                 // ABSIZE
            (6U << 18) |                 // DCYC
            (qspi_mode::QUAD << 24) |    // DMODE
            (qspi_fmode::MEMORY_MAPPED << 26) | 
            (1U << 28)                   // SIOO
        ));
        qspi_invalidate_icache();
        qspi_invalidate_dcache();
        
        std::uint8_t response[10] = {
            0x00, // OK
            flash_sr,
            data[0], data[1], data[2], data[3],
            data[4], data[5], data[6], data[7],
        };
        send_response(dfu::FW_ACK, seq, response, sizeof(response), send_fn);
        return true;
    }

    /// Handle FW_JEDEC_ID: Read JEDEC ID (Manufacturer/Device ID)
    /// Response: [status(1)][mfr_id(1)][device_id(2)][sr(1)][timeout_flag(1)][fifo_level(1)]
    /// IS25LP064A: Manufacturer=0x9D, Device=0x6017
    bool handle_jedec_id(std::uint8_t seq, SendFn send_fn) {
        using namespace ::umi::stm32h7;
        mm::DirectTransportT<> t;
        
        // Exit memory-mapped mode
        qspi_exit_memory_mapped();
        
        // First verify basic communication by reading Status Register
        std::uint8_t sr = qspi_read_byte(is25lp::READ_STATUS);
        
        // Now read JEDEC ID 
        std::uint8_t id[3] = {0};
        std::uint8_t timeout_flag = 0;
        std::uint8_t fifo_level = 0;
        
        qspi_wait_busy();
        t.write(QUADSPI::DLR::value(2)); // 3 bytes (DLR = length - 1)
        
        // RDID command: single line for instruction and data, no address
        t.write(QUADSPI::CCR::value(
            static_cast<std::uint32_t>(is25lp::READ_ID) |  // RDID command (0x9F)
            (qspi_mode::SINGLE << 8) |          // IMODE: single line
            (qspi_mode::NONE << 10) |           // ADMODE: none (no address)
            (qspi_mode::SINGLE << 24) |         // DMODE: single line
            (qspi_fmode::INDIRECT_READ << 26)
        ));
        
        // Wait for transfer complete with timeout
        std::uint32_t timeout = 1000000;
        while (!(t.read(QUADSPI::SR{}) & (1U << 1)) && timeout > 0) { 
            --timeout;
        }
        
        if (timeout == 0) {
            timeout_flag = 1;
        }
        
        // Get FIFO level from SR (bits 13:8 = FLEVEL)
        std::uint32_t sr_val = t.read(QUADSPI::SR{});
        fifo_level = static_cast<std::uint8_t>((sr_val >> 8) & 0x3F);
        
        // Read all data as 32-bit value and extract bytes
        // DR register returns data in LSB-first order for FIFO reads
        std::uint32_t dr_val = t.read(QUADSPI::DR{});
        id[0] = static_cast<std::uint8_t>(dr_val & 0xFF);
        id[1] = static_cast<std::uint8_t>((dr_val >> 8) & 0xFF);
        id[2] = static_cast<std::uint8_t>((dr_val >> 16) & 0xFF);
        
        t.write(QUADSPI::FCR::value(1U << 1)); // Clear TCF
        qspi_wait_busy();
        
        // Re-enter memory-mapped mode
        qspi_enter_memory_mapped();
        
        std::uint8_t response[7] = {
            0x00, // OK
            id[0], id[1], id[2], // Manufacturer ID, Device ID (2 bytes)
            sr,             // Status Register for comparison
            timeout_flag,   // Did timeout occur?
            fifo_level,     // FIFO level after transfer
        };
        send_response(dfu::FW_ACK, seq, response, sizeof(response), send_fn);
        return true;
    }

    /// Handle FW_WRITE_TEST: Simple indirect read test (no write, no wait)
    /// Just read 8 bytes from address 0 via indirect mode
    /// Response: [status(1)][data(8)][fifo_level(1)]
    bool handle_write_test(std::uint8_t seq, SendFn send_fn) {
        using namespace ::umi::stm32h7;
        mm::DirectTransportT<> t;
        
        // Exit memory-mapped mode
        qspi_exit_memory_mapped();
        
        // Clear any leftover flags
        t.write(QUADSPI::FCR::value(0x1F));
        
        // Read 8 bytes via indirect read (single line 0x03)
        std::uint8_t read_data[8] = {0};
        
        qspi_wait_busy();
        t.write(QUADSPI::DLR::value(7)); // 8 bytes - must be BEFORE CCR
        
        // CCR - configure command (this starts the instruction phase)
        // IMPORTANT: CCR must be written BEFORE AR for address-based commands!
        t.write(QUADSPI::CCR::value(
            static_cast<std::uint32_t>(0x03) |  // READ command
            (qspi_mode::SINGLE << 8) |          // IMODE
            (qspi_mode::SINGLE << 10) |         // ADMODE
            (qspi_adsize::ADDR_24BIT << 12) |
            (qspi_mode::SINGLE << 24) |         // DMODE
            (qspi_fmode::INDIRECT_READ << 26)
        ));
        
        // AR - set address (this starts the address phase, then data phase)
        t.write(QUADSPI::AR::value(0));  // Address 0 - must be AFTER CCR!
        
        // Wait for transfer complete with timeout
        std::uint32_t timeout = 100000;
        while (!(t.read(QUADSPI::SR{}) & (1U << 1)) && timeout > 0) {
            --timeout;
        }
        
        // Get FIFO level
        std::uint32_t sr_val = t.read(QUADSPI::SR{});
        std::uint8_t fifo_level = static_cast<std::uint8_t>((sr_val >> 8) & 0x3F);
        
        // Read data from FIFO - read as 32-bit words
        std::uint32_t data0 = t.read(QUADSPI::DR{});
        std::uint32_t data1 = t.read(QUADSPI::DR{});
        read_data[0] = data0 & 0xFF;
        read_data[1] = (data0 >> 8) & 0xFF;
        read_data[2] = (data0 >> 16) & 0xFF;
        read_data[3] = (data0 >> 24) & 0xFF;
        read_data[4] = data1 & 0xFF;
        read_data[5] = (data1 >> 8) & 0xFF;
        read_data[6] = (data1 >> 16) & 0xFF;
        read_data[7] = (data1 >> 24) & 0xFF;
        
        t.write(QUADSPI::FCR::value(1U << 1)); // Clear TCF
        
        // Re-enter memory-mapped mode
        qspi_enter_memory_mapped();
        
        std::uint8_t response[10] = {
            static_cast<std::uint8_t>(timeout > 0 ? 0x00 : 0x01), // OK or timeout
            read_data[0], read_data[1], read_data[2], read_data[3],
            read_data[4], read_data[5], read_data[6], read_data[7],
            fifo_level,
        };
        send_response(dfu::FW_ACK, seq, response, sizeof(response), send_fn);
        return true;
    }

    QspiUpdateState state_ = QspiUpdateState::IDLE;
    std::uint32_t expected_size_ = 0;
    std::uint32_t received_bytes_ = 0;
    std::uint32_t running_crc_ = 0xFFFFFFFF;
    std::uint8_t expected_seq_ = 0;
    PollFn poll_fn_ = nullptr;
};

} // namespace umi::daisy
