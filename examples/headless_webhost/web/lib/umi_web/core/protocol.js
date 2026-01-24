/**
 * UMI Protocol - SysEx encoding/decoding utilities
 *
 * This module provides the same encoding/decoding as lib/umidi/include/protocol/encoding.hh
 * ensuring compatibility between Web and firmware implementations.
 *
 * @module umi_web/core/protocol
 */

// UMI SysEx ID (matches commands.hh)
export const UMI_SYSEX_ID = [0x7E, 0x7F, 0x00];

// Protocol commands (from commands.hh)
export const Command = {
    // Standard IO (0x01-0x0F)
    STDOUT_DATA: 0x01,
    STDERR_DATA: 0x02,
    STDIN_DATA: 0x03,
    STDIN_EOF: 0x04,
    FLOW_CTRL: 0x05,

    // Firmware Update (0x10-0x1F)
    FW_QUERY: 0x10,
    FW_INFO: 0x11,
    FW_BEGIN: 0x12,
    FW_DATA: 0x13,
    FW_VERIFY: 0x14,
    FW_COMMIT: 0x15,
    FW_ROLLBACK: 0x16,
    FW_REBOOT: 0x17,
    FW_ACK: 0x18,
    FW_NACK: 0x19,

    // System (0x20-0x2F)
    PING: 0x20,
    PONG: 0x21,
    RESET: 0x22,
    VERSION: 0x23,
};

/**
 * Encode 8-bit data to 7-bit MIDI-safe encoding.
 * Matches lib/umidi/include/protocol/encoding.hh encode_7bit()
 *
 * Format: For every 7 bytes of input, output 8 bytes:
 *   - 1 MSB byte containing the high bits
 *   - 7 data bytes with high bit cleared
 *
 * @param {Uint8Array|number[]} data - 8-bit input data
 * @returns {number[]} 7-bit encoded data
 */
export function encode7bit(data) {
    const result = [];
    let i = 0;
    while (i < data.length) {
        let msbByte = 0;
        const chunk = [];
        for (let j = 0; j < 7 && i < data.length; j++) {
            const byte = data[i++];
            if (byte & 0x80) {
                msbByte |= (1 << j);
            }
            chunk.push(byte & 0x7F);
        }
        result.push(msbByte);
        result.push(...chunk);
    }
    return result;
}

/**
 * Decode 7-bit MIDI-safe encoding to 8-bit data.
 * Matches lib/umidi/include/protocol/encoding.hh decode_7bit()
 *
 * @param {Uint8Array|number[]} data - 7-bit encoded data
 * @returns {number[]} 8-bit decoded data
 */
export function decode7bit(data) {
    const result = [];
    let i = 0;
    while (i < data.length) {
        const msbByte = data[i++];
        if (msbByte > 0x7F) return result; // Invalid
        for (let j = 0; j < 7 && i < data.length; j++) {
            const byte = data[i++];
            if (byte > 0x7F) return result; // Invalid
            const decoded = byte | ((msbByte >> j) & 1) << 7;
            result.push(decoded);
        }
    }
    return result;
}

/**
 * Calculate XOR checksum.
 * Matches lib/umidi/include/protocol/encoding.hh calculate_checksum()
 *
 * @param {Uint8Array|number[]} data
 * @returns {number} 7-bit checksum
 */
export function calculateChecksum(data) {
    let checksum = 0;
    for (let i = 0; i < data.length; i++) {
        checksum ^= data[i];
    }
    return checksum & 0x7F;
}

/**
 * Build a UMI SysEx message.
 * Matches lib/umidi/include/protocol/message.hh MessageBuilder
 *
 * @param {number} command - Command byte
 * @param {number} sequence - Sequence number (0-127)
 * @param {Uint8Array|number[]} [payload=[]] - 8-bit payload (will be 7-bit encoded)
 * @returns {Uint8Array} Complete SysEx message
 */
export function buildMessage(command, sequence, payload = []) {
    const encoded = encode7bit(payload);
    const cmdData = [command, sequence & 0x7F, ...encoded];
    const checksum = calculateChecksum(cmdData);

    return new Uint8Array([
        0xF0,
        ...UMI_SYSEX_ID,
        ...cmdData,
        checksum,
        0xF7
    ]);
}

/**
 * Parse a UMI SysEx message.
 * Matches lib/umidi/include/protocol/message.hh parse_message()
 *
 * @param {Uint8Array|number[]} data - SysEx data (including F0 and F7)
 * @returns {object|null} Parsed message { command, sequence, payload } or null if invalid
 */
export function parseMessage(data) {
    // Minimum: F0 + ID(3) + CMD + SEQ + CHECKSUM + F7 = 8
    if (data.length < 8) return null;
    if (data[0] !== 0xF0 || data[data.length - 1] !== 0xF7) return null;

    // Check UMI ID
    for (let i = 0; i < UMI_SYSEX_ID.length; i++) {
        if (data[1 + i] !== UMI_SYSEX_ID[i]) return null;
    }

    const cmdPos = 1 + UMI_SYSEX_ID.length;
    const checksumPos = data.length - 2;

    // Verify checksum
    const cmdData = Array.prototype.slice.call(data, cmdPos, checksumPos);
    const expected = data[checksumPos];
    const actual = calculateChecksum(cmdData);
    if (expected !== actual) {
        console.warn('[Protocol] Checksum mismatch:', expected, 'vs', actual);
        return null;
    }

    const command = data[cmdPos];
    const sequence = data[cmdPos + 1];
    const encodedPayload = Array.prototype.slice.call(data, cmdPos + 2, checksumPos);
    const payload = decode7bit(encodedPayload);

    return { command, sequence, payload };
}

/**
 * Parse shell output from SysEx (convenience wrapper).
 *
 * @param {Uint8Array|number[]} data - SysEx data
 * @returns {object|null} { type: 'stdout'|'stderr', text: string } or null
 */
export function parseShellOutput(data) {
    const msg = parseMessage(data);
    if (!msg) return null;

    if (msg.command === Command.STDOUT_DATA || msg.command === Command.STDERR_DATA) {
        const text = new TextDecoder().decode(new Uint8Array(msg.payload));
        return {
            type: msg.command === Command.STDOUT_DATA ? 'stdout' : 'stderr',
            text
        };
    }

    return null;
}
