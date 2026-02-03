// SPDX-License-Identifier: MIT
// umidi - SysEx Assembler (05-midi.md)
// Reassembles UMP SysEx7 packets into complete SysEx messages.
#pragma once

#include "ump.hh"
#include <cstdint>
#include <cstring>
#include <span>

namespace umidi {

/// Reassembles UMP SysEx7 packets (MT=3) into a complete SysEx message.
/// One instance per transport (USB, UART). ~260B per instance.
struct SysExAssembler {
    uint8_t buffer[256] = {};
    uint16_t length = 0;
    bool complete = false;

    /// Feed a UMP64 SysEx7 packet. Call for each MT=3 packet.
    /// SysEx7 status nibble encoding:
    ///   0x0 = Complete message (single packet)
    ///   0x1 = Start
    ///   0x2 = Continue
    ///   0x3 = End
    void feed(const UMP64& ump) noexcept {
        if (ump.mt() != 3) {
            return;
        }

        uint8_t sysex_status = ump.sysex_status();
        uint8_t num_bytes = ump.sysex_num_bytes();

        // Start or Complete: reset buffer
        if (sysex_status == 0x0 || sysex_status == 0x1) {
            length = 0;
            complete = false;
        }

        // Extract data bytes from UMP64 (up to 6 bytes)
        uint8_t bytes[6];
        bytes[0] = (ump.word0 >> 8) & 0xFF;
        bytes[1] = ump.word0 & 0xFF;
        bytes[2] = (ump.word1 >> 24) & 0xFF;
        bytes[3] = (ump.word1 >> 16) & 0xFF;
        bytes[4] = (ump.word1 >> 8) & 0xFF;
        bytes[5] = ump.word1 & 0xFF;

        // Append bytes to buffer (clamped to capacity)
        for (uint8_t i = 0; i < num_bytes && length < sizeof(buffer); ++i) {
            buffer[length++] = bytes[i];
        }

        // Complete or End: mark as complete
        if (sysex_status == 0x0 || sysex_status == 0x3) {
            complete = true;
        }
    }

    /// Overload for UMP32-based SysEx routing (when UMP32 carries SysEx start/end markers).
    /// For full SysEx7, use the UMP64 overload.
    void feed(const UMP32& ump) noexcept {
        // UMP32 doesn't carry SysEx7 payload (MT=3 is always 64-bit).
        // This overload handles legacy F0/F7 status tracking if needed.
        uint8_t status = ump.status();
        if (status == 0xF0) {
            // SysEx start: reset
            length = 0;
            complete = false;
        } else if (status == 0xF7) {
            // SysEx end: mark complete
            complete = true;
        }
    }

    [[nodiscard]] bool is_complete() const noexcept { return complete; }

    [[nodiscard]] std::span<const uint8_t> data() const noexcept {
        return {buffer, length};
    }

    void reset() noexcept {
        length = 0;
        complete = false;
    }
};

static_assert(sizeof(SysExAssembler) <= 264, "SysExAssembler should be ~260B");

} // namespace umidi
