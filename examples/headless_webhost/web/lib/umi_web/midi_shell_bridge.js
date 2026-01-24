/**
 * UMI Web - MIDI Shell Bridge
 *
 * Connects Web MIDI API to UMI-OS shell over SysEx.
 * Enables terminal interaction with physical UMI devices via USB MIDI.
 *
 * @module umi_web/midi_shell_bridge
 */

import { encode7bit, decode7bit } from './components/shell/index.js';

// UMI SysEx ID: 0x7E 0x7F 0x00 (matches commands.hh)
const UMI_SYSEX_ID = [0x7E, 0x7F, 0x00];

// Commands (from commands.hh)
const Command = {
    STDOUT_DATA: 0x01,
    STDERR_DATA: 0x02,
    STDIN_DATA: 0x03,
    STDIN_EOF: 0x04,
    FLOW_CTRL: 0x05,
    PING: 0x20,
    PONG: 0x21,
    RESET: 0x22,
    VERSION: 0x23,
};

/**
 * Calculate UMI protocol checksum
 * @param {Uint8Array|number[]} data
 * @returns {number}
 */
function calculateChecksum(data) {
    let sum = 0;
    for (let i = 0; i < data.length; i++) {
        sum += data[i];
    }
    return sum & 0x7F;
}

/**
 * Build a UMI SysEx message
 * @param {number} command
 * @param {number} sequence
 * @param {Uint8Array|number[]} payload - 8-bit data (will be 7-bit encoded)
 * @returns {Uint8Array}
 */
function buildMessage(command, sequence, payload = []) {
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
 * Parse a UMI SysEx message
 * @param {Uint8Array} data
 * @returns {object|null}
 */
function parseMessage(data) {
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
    const cmdData = data.slice(cmdPos, checksumPos);
    const expected = data[checksumPos];
    const actual = calculateChecksum(cmdData);
    if (expected !== actual) {
        console.warn('UMI SysEx checksum mismatch:', expected, 'vs', actual);
        return null;
    }

    const command = data[cmdPos];
    const sequence = data[cmdPos + 1];
    const encodedPayload = data.slice(cmdPos + 2, checksumPos);
    const payload = decode7bit(encodedPayload);

    return { command, sequence, payload };
}

/**
 * MIDI Shell Bridge - connects Shell UI to physical UMI device
 */
export class MidiShellBridge {
    /**
     * @param {Shell} shell - Shell component instance
     */
    constructor(shell) {
        this.shell = shell;
        this.midiAccess = null;
        this.input = null;
        this.output = null;
        this.connected = false;
        this.txSequence = 0;

        // Device filter
        this.deviceNameFilter = 'UMI';
    }

    /**
     * Request MIDI access and list devices
     * @returns {Promise<MIDIAccess>}
     */
    async requestAccess() {
        if (!navigator.requestMIDIAccess) {
            throw new Error('Web MIDI API not supported');
        }

        this.midiAccess = await navigator.requestMIDIAccess({ sysex: true });
        this.midiAccess.onstatechange = (e) => this._onStateChange(e);
        return this.midiAccess;
    }

    /**
     * Get available UMI devices
     * @returns {Array<{input: MIDIInput, output: MIDIOutput, name: string}>}
     */
    getDevices() {
        if (!this.midiAccess) return [];

        const devices = [];
        const inputs = new Map();
        const outputs = new Map();

        // Collect inputs/outputs
        for (const input of this.midiAccess.inputs.values()) {
            if (input.name && input.name.includes(this.deviceNameFilter)) {
                inputs.set(input.name, input);
            }
        }
        for (const output of this.midiAccess.outputs.values()) {
            if (output.name && output.name.includes(this.deviceNameFilter)) {
                outputs.set(output.name, output);
            }
        }

        // Match pairs
        for (const [name, input] of inputs) {
            const output = outputs.get(name);
            if (output) {
                devices.push({ input, output, name });
            }
        }

        return devices;
    }

    /**
     * Connect to a specific device
     * @param {string} deviceName
     * @returns {boolean}
     */
    connect(deviceName) {
        const devices = this.getDevices();
        const device = devices.find(d => d.name === deviceName);

        if (!device) {
            this.shell.write(`Device not found: ${deviceName}`, 'stderr');
            return false;
        }

        this.input = device.input;
        this.output = device.output;

        // Set up MIDI message handler
        this.input.onmidimessage = (e) => this._onMidiMessage(e);

        this.connected = true;
        this.shell.write(`Connected to: ${deviceName}`, 'system');
        this.shell.enable();

        // Send shell start command (empty line to trigger prompt)
        this.sendCommand('');

        return true;
    }

    /**
     * Disconnect from current device
     */
    disconnect() {
        if (this.input) {
            this.input.onmidimessage = null;
            this.input = null;
        }
        this.output = null;
        this.connected = false;
        this.shell.disable();
        this.shell.write('Disconnected', 'system');
    }

    /**
     * Send command to device
     * @param {string} command
     */
    sendCommand(command) {
        if (!this.connected || !this.output) {
            this.shell.write('Not connected', 'stderr');
            return;
        }

        // Encode command as UTF-8
        const encoder = new TextEncoder();
        const data = encoder.encode(command + '\r');

        // Build SysEx message
        const msg = buildMessage(Command.STDIN_DATA, this.txSequence++, data);

        // Send via Web MIDI
        this.output.send(msg);
    }

    /**
     * Send ping to check connection
     */
    sendPing() {
        if (!this.connected || !this.output) return;

        const msg = buildMessage(Command.PING, this.txSequence++, []);
        this.output.send(msg);
    }

    /**
     * Send reset command
     */
    sendReset() {
        if (!this.connected || !this.output) return;

        const msg = buildMessage(Command.RESET, this.txSequence++, []);
        this.output.send(msg);
    }

    /**
     * Handle MIDI messages
     * @private
     */
    _onMidiMessage(event) {
        const data = event.data;

        // Check for SysEx
        if (data[0] !== 0xF0) return;

        const msg = parseMessage(data);
        if (!msg) return;

        switch (msg.command) {
            case Command.STDOUT_DATA: {
                const text = new TextDecoder().decode(new Uint8Array(msg.payload));
                this.shell.write(text, 'stdout');
                break;
            }
            case Command.STDERR_DATA: {
                const text = new TextDecoder().decode(new Uint8Array(msg.payload));
                this.shell.write(text, 'stderr');
                break;
            }
            case Command.PONG: {
                this.shell.write('PONG received', 'system');
                break;
            }
            case Command.VERSION: {
                if (msg.payload.length >= 2) {
                    this.shell.write(`Version: ${msg.payload[0]}.${msg.payload[1]}`, 'system');
                }
                break;
            }
            default:
                console.log('Unknown UMI command:', msg.command);
        }
    }

    /**
     * Handle MIDI state changes
     * @private
     */
    _onStateChange(event) {
        const port = event.port;

        if (port.state === 'disconnected') {
            if (this.input && port.id === this.input.id) {
                this.disconnect();
                this.shell.write('Device disconnected', 'stderr');
            }
        }
    }
}

/**
 * Create and initialize a MIDI Shell Bridge
 * @param {Shell} shell - Shell component
 * @returns {Promise<MidiShellBridge>}
 */
export async function createMidiShellBridge(shell) {
    const bridge = new MidiShellBridge(shell);
    await bridge.requestAccess();

    // Connect shell command handler
    shell.onCommand = (cmd) => {
        bridge.sendCommand(cmd);
    };

    return bridge;
}
