/**
 * UMI Web - Hardware Backend
 *
 * Connects to real UMI hardware via Web MIDI API.
 * Audio comes from the hardware, not browser synthesis.
 * Shell interaction via SysEx messages.
 *
 * @module umi_web/core/backends/hardware
 */

import { BackendInterface } from '../backend.js';
import { Command, buildMessage, parseMessage } from '../protocol.js';

/**
 * Hardware Backend
 *
 * Connects to UMI hardware via USB MIDI.
 * - MIDI IN/OUT for note/CC control
 * - SysEx for shell communication
 * - Audio from hardware (not browser)
 */
export class HardwareBackend extends BackendInterface {
    /**
     * @param {object} options
     * @param {string} [options.deviceNameFilter='UMI'] - Filter for device names
     */
    constructor(options = {}) {
        super();
        this.deviceNameFilter = options.deviceNameFilter || 'UMI';

        /** @type {MIDIAccess|null} */
        this.midiAccess = null;
        /** @type {MIDIInput|null} */
        this.input = null;
        /** @type {MIDIOutput|null} */
        this.output = null;

        this.connected = false;
        this.txSequence = 0;
        this.deviceName = null;

        // Callbacks for shell output
        /** @type {function|null} */
        this.onShellOutput = null;
    }

    /**
     * Check if Web MIDI is supported
     * @returns {boolean}
     */
    static isSupported() {
        return typeof navigator !== 'undefined' && 'requestMIDIAccess' in navigator;
    }

    /**
     * Check if hardware backend is available
     * @returns {Promise<boolean>}
     */
    static async isAvailable() {
        if (!HardwareBackend.isSupported()) return false;
        try {
            const access = await navigator.requestMIDIAccess({ sysex: true });
            // Check if any UMI device is connected
            for (const input of access.inputs.values()) {
                if (input.name && input.name.includes('UMI')) {
                    return true;
                }
            }
            return false;
        } catch {
            return false;
        }
    }

    /**
     * Get available UMI devices
     * @returns {Promise<Array<{input: MIDIInput, output: MIDIOutput, name: string}>>}
     */
    async getDevices() {
        if (!this.midiAccess) {
            await this._requestAccess();
        }
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
     * Request MIDI access
     * @private
     */
    async _requestAccess() {
        if (!HardwareBackend.isSupported()) {
            throw new Error('Web MIDI API not supported');
        }
        this.midiAccess = await navigator.requestMIDIAccess({ sysex: true });
        this.midiAccess.onstatechange = (e) => this._onStateChange(e);
    }

    /**
     * Start the backend - connect to first available device
     * @returns {Promise<boolean>}
     */
    async start() {
        const devices = await this.getDevices();
        if (devices.length === 0) {
            if (this.onError) {
                this.onError(new Error('No UMI device found'));
            }
            return false;
        }
        return this.connectDevice(devices[0].name);
    }

    /**
     * Connect to a specific device by name
     * @param {string} deviceName
     * @returns {Promise<boolean>}
     */
    async connectDevice(deviceName) {
        const devices = await this.getDevices();
        const device = devices.find(d => d.name === deviceName);

        if (!device) {
            if (this.onError) {
                this.onError(new Error(`Device not found: ${deviceName}`));
            }
            return false;
        }

        this.input = device.input;
        this.output = device.output;
        this.deviceName = deviceName;

        // Set up MIDI message handler
        this.input.onmidimessage = (e) => this._onMidiMessage(e);

        this.connected = true;
        console.log('[HardwareBackend] Connected to:', deviceName);

        if (this.onReady) {
            this.onReady();
        }

        // Send initial command to trigger shell prompt
        this._sendShellInput('\r');

        return true;
    }

    /**
     * Stop the backend
     */
    stop() {
        if (this.input) {
            this.input.onmidimessage = null;
            this.input = null;
        }
        this.output = null;
        this.connected = false;
        this.deviceName = null;
        console.log('[HardwareBackend] Disconnected');
    }

    /**
     * Send raw MIDI data
     * @param {Uint8Array|number[]} data
     */
    sendMidi(data) {
        if (!this.connected || !this.output) return;
        this.output.send(data);
    }

    /**
     * Send Note On
     * @param {number} note
     * @param {number} velocity
     */
    noteOn(note, velocity) {
        this.sendMidi([0x90, note & 0x7F, velocity & 0x7F]);
    }

    /**
     * Send Note Off
     * @param {number} note
     */
    noteOff(note) {
        this.sendMidi([0x80, note & 0x7F, 0]);
    }

    /**
     * Set parameter value (CC)
     * @param {number} id - CC number
     * @param {number} value - CC value (0-127)
     */
    setParam(id, value) {
        this.sendMidi([0xB0, id & 0x7F, value & 0x7F]);
    }

    /**
     * Send shell command
     * @param {string} text
     */
    sendShellCommand(text) {
        this._sendShellInput(text + '\r');
    }

    /**
     * Send shell input data
     * @private
     */
    _sendShellInput(text) {
        if (!this.connected || !this.output) return;

        const encoder = new TextEncoder();
        const data = encoder.encode(text);
        const msg = buildMessage(Command.STDIN_DATA, this.txSequence++, data);
        console.log('[HardwareBackend] Sending SysEx:', Array.from(msg).map(b => b.toString(16).padStart(2, '0')).join(' '));
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
    reset() {
        if (!this.connected || !this.output) return;
        const msg = buildMessage(Command.RESET, this.txSequence++, []);
        this.output.send(msg);
    }

    /**
     * Get application info
     * @returns {object}
     */
    getAppInfo() {
        return {
            name: this.deviceName || 'Hardware',
            vendor: 'UMI',
            version: '1.0.0'
        };
    }

    /**
     * Check if connected
     * @returns {boolean}
     */
    isPlaying() {
        return this.connected;
    }

    /**
     * Get state - not available for hardware
     */
    getState() {
        return null;
    }

    /**
     * Handle MIDI messages
     * @private
     */
    _onMidiMessage(event) {
        const data = event.data;
        if (!data || data.length === 0) return;

        // Check for SysEx
        if (data[0] === 0xF0) {
            console.log('[HardwareBackend] Received SysEx:', Array.from(data).map(b => b.toString(16).padStart(2, '0')).join(' '));
            const msg = parseMessage(data);
            console.log('[HardwareBackend] Parsed message:', msg);
            if (msg) {
                this._handleSysEx(msg);
            } else {
                console.warn('[HardwareBackend] Failed to parse SysEx');
            }
            return;
        }

        // Regular MIDI - forward to onMessage
        if (this.onMessage) {
            this.onMessage({ type: 'midi', data: Array.from(data) });
        }
    }

    /**
     * Handle SysEx messages
     * @private
     */
    _handleSysEx(msg) {
        switch (msg.command) {
            case Command.STDOUT_DATA: {
                const text = new TextDecoder().decode(new Uint8Array(msg.payload));
                if (this.onShellOutput) {
                    this.onShellOutput(text, 'stdout');
                }
                if (this.onMessage) {
                    this.onMessage({ type: 'shell', stream: 'stdout', text });
                }
                break;
            }
            case Command.STDERR_DATA: {
                const text = new TextDecoder().decode(new Uint8Array(msg.payload));
                if (this.onShellOutput) {
                    this.onShellOutput(text, 'stderr');
                }
                if (this.onMessage) {
                    this.onMessage({ type: 'shell', stream: 'stderr', text });
                }
                break;
            }
            case Command.PONG: {
                if (this.onMessage) {
                    this.onMessage({ type: 'pong' });
                }
                break;
            }
            case Command.VERSION: {
                if (msg.payload.length >= 2) {
                    const version = `${msg.payload[0]}.${msg.payload[1]}`;
                    if (this.onMessage) {
                        this.onMessage({ type: 'version', version });
                    }
                }
                break;
            }
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
                this.stop();
                if (this.onError) {
                    this.onError(new Error('Device disconnected'));
                }
            }
        }
    }
}
