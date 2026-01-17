/**
 * UMI Web - Hardware Simulation Backends
 *
 * Backends for hardware simulation:
 * - RenodeBackend: Cycle-accurate hardware simulation via WebSocket
 * - CortexMBackend: Pure JS Cortex-M emulator (placeholder)
 *
 * @module umi_web/core/backends/renode
 */

import { BackendInterface } from '../backend.js';

/**
 * Renode Backend - WebSocket to Renode Bridge
 *
 * Connects to Renode hardware simulation server for cycle-accurate
 * emulation of embedded hardware.
 */
export class RenodeBackend extends BackendInterface {
    /**
     * @param {object} options
     * @param {string} [options.wsUrl='ws://localhost:8089']
     * @param {object} [options.adapter] - Custom RenodeAdapter instance
     */
    constructor(options = {}) {
        super();
        this.wsUrl = options.wsUrl || 'ws://localhost:8089';
        this.adapter = options.adapter || null;
        this.analyzerNode = null;
        this._adapterModule = null;
    }

    async start() {
        // Lazy load RenodeAdapter
        if (!this.adapter) {
            try {
                // Try to import from web root (relative to HTML file)
                // Path: lib/umi_web/core/backends/ -> ../../../renode_adapter.js
                const module = await import('../../../../renode_adapter.js');
                this._adapterModule = module;
                this.adapter = new module.RenodeAdapter(this.wsUrl);
            } catch (err) {
                throw new Error(`Failed to load RenodeAdapter: ${err.message}`);
            }
        }

        // Set up callbacks
        this.adapter.onMessage = (msg) => {
            if (msg.type === 'ready' && this.onReady) {
                this.onReady();
            }
            if (this.onMessage) this.onMessage(msg);
        };

        this.adapter.onError = (err) => {
            if (this.onError) this.onError(err.message || 'Connection error');
        };

        // Connect to WebSocket
        try {
            await this.adapter.connect();
        } catch (err) {
            throw new Error('Cannot connect to Renode Web Bridge. Is web_bridge.py running?');
        }

        // Start and wait for actual audio from Renode
        try {
            await this.adapter.start();
        } catch (err) {
            throw new Error(err.message || 'Failed to start Renode audio');
        }

        // Create analyzer for waveform
        this.analyzerNode = this.adapter.createAnalyzer();

        return true;
    }

    stop() {
        if (this.adapter) {
            this.adapter.stopAudio();
            this.adapter.disconnect();
        }
    }

    sendMidi(data) {
        if (this.adapter) {
            this.adapter.sendMidi(data);
        }
    }

    noteOn(note, velocity) {
        if (this.adapter) {
            this.adapter.noteOn(note, velocity);
        }
    }

    noteOff(note) {
        if (this.adapter) {
            this.adapter.noteOff(note);
        }
    }

    setParam(id, value) {
        if (this.adapter) {
            this.adapter.setParam(id, value);
        }
    }

    getState() {
        if (this.adapter) {
            return this.adapter.getKernelState();
        }
        return null;
    }

    getAudioContext() {
        return this.adapter ? this.adapter.getAudioContext() : null;
    }

    getAnalyzer() {
        return this.analyzerNode;
    }

    isPlaying() {
        return this.adapter ? this.adapter.isPlaying : false;
    }

    /**
     * Check if Renode bridge is available
     * @param {string} [wsUrl='ws://localhost:8089']
     * @returns {Promise<boolean>}
     */
    static async isAvailable(wsUrl = 'ws://localhost:8089') {
        try {
            const ws = new WebSocket(wsUrl);
            await new Promise((resolve, reject) => {
                ws.onopen = () => { ws.close(); resolve(); };
                ws.onerror = reject;
                setTimeout(reject, 1000);
            });
            return true;
        } catch {
            return false;
        }
    }
}

/**
 * Cortex-M Backend - Pure JavaScript emulator (rp2040js/thumbulator)
 *
 * NOTE: This is a placeholder for future implementation.
 * Would require:
 * 1. Loading rp2040js or thumbulator.ts library
 * 2. Loading compiled ARM binary (ELF/BIN)
 * 3. Setting up peripheral emulation for I2S, UART, etc.
 * 4. Bridging audio output to WebAudio
 */
export class CortexMBackend extends BackendInterface {
    /**
     * @param {object} options
     * @param {string} [options.elfUrl='../synth/synth_example.elf']
     * @param {number} [options.cpuFreq=168000000]
     */
    constructor(options = {}) {
        super();
        this.elfUrl = options.elfUrl || '../synth/synth_example.elf';
        this.cpuFreq = options.cpuFreq || 168000000; // 168 MHz

        // Emulator state
        this.cpu = null;
        this.memory = null;
        this.peripherals = {};

        // Audio
        this.audioContext = null;
        this.audioBuffers = [];

        this._isPlaying = false;
        this._available = false;
    }

    /**
     * Check if Cortex-M emulator is available
     * @returns {Promise<boolean>}
     */
    static async isAvailable() {
        try {
            return typeof window.RP2040 !== 'undefined' ||
                   typeof window.Thumbulator !== 'undefined';
        } catch {
            return false;
        }
    }

    async start() {
        // Check availability
        if (!await CortexMBackend.isAvailable()) {
            throw new Error('Cortex-M emulator not available. Load rp2040js or thumbulator first.');
        }

        // This is a placeholder implementation
        console.warn('[CortexMBackend] Not yet implemented - using stub');

        this._isPlaying = true;

        // Simulate ready
        if (this.onReady) {
            setTimeout(() => this.onReady(), 100);
        }

        return true;
    }

    stop() {
        this._isPlaying = false;
        if (this.audioContext) {
            this.audioContext.close();
            this.audioContext = null;
        }
    }

    sendMidi(data) {
        // Would inject into emulated UART/USB
        console.log('[CortexMBackend] MIDI:', data);
    }

    noteOn(note, velocity) {
        this.sendMidi([0x90, note, velocity]);
    }

    noteOff(note) {
        this.sendMidi([0x80, note, 0]);
    }

    setParam(id, value) {
        // Would write to emulated memory or send via protocol
        console.log('[CortexMBackend] Set param', id, value);
    }

    getState() {
        // Would read from emulated CPU/memory
        return {
            uptime: Date.now() * 1000,
            dspLoad: 0,
            midiRx: 0,
            midiTx: 0,
            audioRunning: this._isPlaying,
        };
    }

    getAudioContext() { return this.audioContext; }
    getAnalyzer() { return null; }
    isPlaying() { return this._isPlaying; }
}
