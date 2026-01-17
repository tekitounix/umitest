/**
 * UMI Web - MIDI Device Selector
 *
 * Manages WebMIDI input/output device selection.
 * Supports multi-device selection and MIDI Thru routing.
 *
 * @module umi_web/midi/device-selector
 */

/**
 * MIDI Device Selector
 *
 * Handles WebMIDI device enumeration and multi-selection with localStorage persistence.
 */
export class MidiDeviceSelector {
    /**
     * @param {object} options
     * @param {string} [options.storageKeyInputs='umi-midi-inputs']
     * @param {string} [options.storageKeyOutputs='umi-midi-outputs']
     * @param {boolean} [options.sysex=true] - Request SysEx permission
     * @param {boolean} [options.autoSelectAll=true] - Auto-select all devices if no saved selection
     */
    constructor(options = {}) {
        this.storageKeyInputs = options.storageKeyInputs || 'umi-midi-inputs';
        this.storageKeyOutputs = options.storageKeyOutputs || 'umi-midi-outputs';
        this.sysex = options.sysex !== false;
        this.autoSelectAll = options.autoSelectAll !== false;

        /** @type {MIDIAccess|null} */
        this.midiAccess = null;

        /** @type {Map<string, MIDIInput>} Connected inputs */
        this.connectedInputs = new Map();
        /** @type {Map<string, MIDIOutput>} Connected outputs */
        this.connectedOutputs = new Map();

        // MIDI Thru settings
        this.thruEnabled = false;
        this.thruChannel = 0; // 0 = all channels

        // Callbacks
        /** @type {function|null} */
        this.onDevicesChanged = null;
        /** @type {function|null} */
        this.onMidiMessage = null;
        /** @type {function|null} */
        this.onError = null;
        /** @type {function|null} */
        this.onStateChange = null;

        this._messageHandler = this._handleMidiMessage.bind(this);
    }

    /**
     * Check if WebMIDI is supported
     * @returns {boolean}
     */
    static isSupported() {
        return typeof navigator !== 'undefined' && 'requestMIDIAccess' in navigator;
    }

    /**
     * Initialize WebMIDI
     * @returns {Promise<boolean>}
     */
    async init() {
        if (!MidiDeviceSelector.isSupported()) {
            if (this.onError) this.onError(new Error('WebMIDI not supported'));
            return false;
        }

        try {
            this.midiAccess = await navigator.requestMIDIAccess({ sysex: this.sysex });

            // Listen for device changes
            this.midiAccess.onstatechange = (e) => {
                if (this.onStateChange) {
                    this.onStateChange(e.port);
                }
                this._updateDevices();
            };

            // Initial device setup
            this._updateDevices();

            return true;
        } catch (err) {
            if (this.onError) this.onError(err);
            return false;
        }
    }

    /**
     * Cleanup resources
     */
    destroy() {
        this.disconnectAllInputs();
        this.disconnectAllOutputs();
        if (this.midiAccess) {
            this.midiAccess.onstatechange = null;
        }
    }

    /**
     * Get all available input devices
     * @returns {MIDIInput[]}
     */
    getInputDevices() {
        if (!this.midiAccess) return [];
        return Array.from(this.midiAccess.inputs.values());
    }

    /**
     * Get all available output devices
     * @returns {MIDIOutput[]}
     */
    getOutputDevices() {
        if (!this.midiAccess) return [];
        return Array.from(this.midiAccess.outputs.values());
    }

    /**
     * Get connected input IDs
     * @returns {string[]}
     */
    getConnectedInputIds() {
        return Array.from(this.connectedInputs.keys());
    }

    /**
     * Get connected output IDs
     * @returns {string[]}
     */
    getConnectedOutputIds() {
        return Array.from(this.connectedOutputs.keys());
    }

    /**
     * Connect a MIDI input
     * @param {string} inputId
     * @returns {boolean}
     */
    connectInput(inputId) {
        if (!this.midiAccess || this.connectedInputs.has(inputId)) return false;

        const input = this.midiAccess.inputs.get(inputId);
        if (input) {
            input.onmidimessage = this._messageHandler;
            this.connectedInputs.set(inputId, input);
            this._saveInputs();
            return true;
        }
        return false;
    }

    /**
     * Disconnect a MIDI input
     * @param {string} inputId
     * @returns {boolean}
     */
    disconnectInput(inputId) {
        const input = this.connectedInputs.get(inputId);
        if (input) {
            input.onmidimessage = null;
            this.connectedInputs.delete(inputId);
            this._saveInputs();
            return true;
        }
        return false;
    }

    /**
     * Disconnect all MIDI inputs
     */
    disconnectAllInputs() {
        for (const input of this.connectedInputs.values()) {
            input.onmidimessage = null;
        }
        this.connectedInputs.clear();
    }

    /**
     * Connect a MIDI output
     * @param {string} outputId
     * @returns {boolean}
     */
    connectOutput(outputId) {
        if (!this.midiAccess || this.connectedOutputs.has(outputId)) return false;

        const output = this.midiAccess.outputs.get(outputId);
        if (output) {
            this.connectedOutputs.set(outputId, output);
            this._saveOutputs();
            return true;
        }
        return false;
    }

    /**
     * Disconnect a MIDI output
     * @param {string} outputId
     * @returns {boolean}
     */
    disconnectOutput(outputId) {
        if (this.connectedOutputs.has(outputId)) {
            this.connectedOutputs.delete(outputId);
            this._saveOutputs();
            return true;
        }
        return false;
    }

    /**
     * Disconnect all MIDI outputs
     */
    disconnectAllOutputs() {
        this.connectedOutputs.clear();
    }

    /**
     * Check if input is connected
     * @param {string} inputId
     * @returns {boolean}
     */
    isInputConnected(inputId) {
        return this.connectedInputs.has(inputId);
    }

    /**
     * Check if output is connected
     * @param {string} outputId
     * @returns {boolean}
     */
    isOutputConnected(outputId) {
        return this.connectedOutputs.has(outputId);
    }

    /**
     * Send MIDI data to all connected outputs
     * @param {Uint8Array|number[]} data
     */
    send(data) {
        for (const output of this.connectedOutputs.values()) {
            try {
                output.send(data);
            } catch (err) {
                console.warn('[MidiDeviceSelector] Send error:', err);
            }
        }
    }

    /**
     * Send MIDI Thru (forward input to outputs)
     * @param {Uint8Array|number[]} data
     */
    sendThru(data) {
        if (!this.thruEnabled || this.connectedOutputs.size === 0) return;

        // Check channel filter
        if (this.thruChannel > 0 && data.length > 0) {
            const status = data[0];
            if (status >= 0x80 && status < 0xF0) {
                const channel = (status & 0x0F) + 1;
                if (channel !== this.thruChannel) return;
            }
        }

        this.send(data);
    }

    /**
     * Set MIDI Thru enabled state
     * @param {boolean} enabled
     */
    setThruEnabled(enabled) {
        this.thruEnabled = enabled;
    }

    /**
     * Set MIDI Thru channel filter
     * @param {number} channel - 0 for all, 1-16 for specific channel
     */
    setThruChannel(channel) {
        this.thruChannel = channel;
    }

    // Private methods

    _updateDevices() {
        const savedInputs = this._loadSetting(this.storageKeyInputs) || [];
        const savedOutputs = this._loadSetting(this.storageKeyOutputs) || [];

        // Auto-connect based on saved preferences or auto-select-all
        const inputs = this.getInputDevices();
        const outputs = this.getOutputDevices();

        for (const input of inputs) {
            const shouldConnect = savedInputs.includes(input.id) ||
                                 (savedInputs.length === 0 && this.autoSelectAll);
            if (shouldConnect && !this.connectedInputs.has(input.id)) {
                this.connectInput(input.id);
            }
        }

        for (const output of outputs) {
            const shouldConnect = savedOutputs.includes(output.id) ||
                                 (savedOutputs.length === 0 && this.autoSelectAll);
            if (shouldConnect && !this.connectedOutputs.has(output.id)) {
                this.connectOutput(output.id);
            }
        }

        // Remove disconnected devices
        for (const [id] of this.connectedInputs) {
            if (!this.midiAccess.inputs.has(id)) {
                this.disconnectInput(id);
            }
        }
        for (const [id] of this.connectedOutputs) {
            if (!this.midiAccess.outputs.has(id)) {
                this.disconnectOutput(id);
            }
        }

        if (this.onDevicesChanged) {
            this.onDevicesChanged({
                inputs: inputs,
                outputs: outputs,
                connectedInputs: this.getConnectedInputIds(),
                connectedOutputs: this.getConnectedOutputIds()
            });
        }
    }

    _handleMidiMessage(event) {
        const data = event.data;
        if (!data || data.length === 0) return;

        // MIDI Thru
        this.sendThru(data);

        // Notify callback
        if (this.onMidiMessage) {
            this.onMidiMessage(data, event.target);
        }
    }

    _saveInputs() {
        this._saveSetting(this.storageKeyInputs, this.getConnectedInputIds());
    }

    _saveOutputs() {
        this._saveSetting(this.storageKeyOutputs, this.getConnectedOutputIds());
    }

    _loadSetting(key) {
        try {
            const saved = localStorage.getItem(key);
            return saved ? JSON.parse(saved) : null;
        } catch {
            return null;
        }
    }

    _saveSetting(key, value) {
        try {
            localStorage.setItem(key, JSON.stringify(value));
        } catch {}
    }
}

/**
 * Parse MIDI message to human-readable format
 * @param {Uint8Array|number[]} data
 * @returns {object}
 */
export function parseMidiMessage(data) {
    if (!data || data.length === 0) return { type: 'unknown' };

    const status = data[0];
    const channel = (status & 0x0F) + 1;
    const messageType = status & 0xF0;

    switch (messageType) {
        case 0x80:
            return {
                type: 'noteOff',
                channel,
                note: data[1],
                velocity: data[2]
            };
        case 0x90:
            return {
                type: data[2] > 0 ? 'noteOn' : 'noteOff',
                channel,
                note: data[1],
                velocity: data[2]
            };
        case 0xA0:
            return {
                type: 'aftertouch',
                channel,
                note: data[1],
                pressure: data[2]
            };
        case 0xB0:
            return {
                type: 'cc',
                channel,
                controller: data[1],
                value: data[2]
            };
        case 0xC0:
            return {
                type: 'programChange',
                channel,
                program: data[1]
            };
        case 0xD0:
            return {
                type: 'channelPressure',
                channel,
                pressure: data[1]
            };
        case 0xE0:
            return {
                type: 'pitchBend',
                channel,
                value: (data[2] << 7) | data[1]
            };
        case 0xF0:
            if (status === 0xF0) {
                return { type: 'sysex', data: Array.from(data) };
            }
            return { type: 'system', status };
        default:
            return { type: 'unknown', data: Array.from(data) };
    }
}

/**
 * Format MIDI message for display
 * @param {Uint8Array|number[]} data
 * @returns {string}
 */
export function formatMidiMessage(data) {
    const parsed = parseMidiMessage(data);

    switch (parsed.type) {
        case 'noteOn':
            return `Note On  Ch${parsed.channel} ${noteToName(parsed.note)} vel=${parsed.velocity}`;
        case 'noteOff':
            return `Note Off Ch${parsed.channel} ${noteToName(parsed.note)}`;
        case 'cc':
            return `CC       Ch${parsed.channel} cc${parsed.controller}=${parsed.value}`;
        case 'programChange':
            return `Program  Ch${parsed.channel} prg=${parsed.program}`;
        case 'pitchBend':
            return `Pitch    Ch${parsed.channel} ${parsed.value}`;
        case 'aftertouch':
            return `Aftertouch Ch${parsed.channel} ${noteToName(parsed.note)} ${parsed.pressure}`;
        case 'channelPressure':
            return `Pressure Ch${parsed.channel} ${parsed.pressure}`;
        case 'sysex':
            return `SysEx    ${data.length} bytes`;
        default:
            return `MIDI     ${Array.from(data).map(b => b.toString(16).padStart(2, '0')).join(' ')}`;
    }
}

/**
 * Convert MIDI note number to note name
 * @param {number} note
 * @returns {string}
 */
export function noteToName(note) {
    const names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
    const octave = Math.floor(note / 12) - 1;
    const name = names[note % 12];
    return `${name}${octave}`.padEnd(4);
}
