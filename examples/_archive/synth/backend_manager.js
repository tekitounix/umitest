/**
 * UMI-OS Backend Manager
 *
 * Manages multiple simulation backends with unified interface:
 *
 * 1. WASM Backend (default)
 *    - Fastest, real-time audio
 *    - Cross-compiled embedded code runs directly in browser
 *    - No network dependency
 *
 * 2. Renode Backend
 *    - Cycle-accurate hardware simulation
 *    - Requires Renode + Web Bridge server running locally
 *    - Best for debugging timing-critical code
 *
 * 3. Web Cortex-M Backend (planned)
 *    - Pure JavaScript Cortex-M emulator (rp2040js/thumbulator.ts)
 *    - Runs actual ARM binaries in browser
 *    - No server required, but slower than WASM
 *
 * Architecture:
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                    BackendManager                                   │
 * │  ┌─────────────────────────────────────────────────────────────┐   │
 * │  │                 Unified API                                  │   │
 * │  │  - start()     - stop()      - sendMidi()                   │   │
 * │  │  - setParam()  - getState()  - onMessage()                  │   │
 * │  └─────────────────────────────────────────────────────────────┘   │
 * │                              │                                      │
 * │         ┌────────────────────┼────────────────────┐                 │
 * │         │                    │                    │                 │
 * │         ▼                    ▼                    ▼                 │
 * │  ┌────────────┐      ┌────────────┐      ┌────────────┐            │
 * │  │   WASM     │      │   Renode   │      │  CortexM   │            │
 * │  │  Backend   │      │  Backend   │      │  Backend   │            │
 * │  │            │      │            │      │            │            │
 * │  │ AudioWork- │      │ WebSocket  │      │ rp2040js/  │            │
 * │  │ let+WASM   │      │ + Bridge   │      │ thumbulator│            │
 * │  └────────────┘      └────────────┘      └────────────┘            │
 * └─────────────────────────────────────────────────────────────────────┘
 */

import { RenodeAdapter } from './renode_adapter.js';

// Backend types
export const BackendType = {
    UMIM: 'umim',           // DSP only (synth_final.js)
    UMIOS: 'umios',         // Full UMI-OS kernel simulation (synth_worklet.js + synth_sim.wasm)
    RENODE: 'renode',       // Renode hardware simulation
    // Legacy alias - WASM maps to UMIOS for backward compatibility with synth_sim.html
    WASM: 'umios',
};

/**
 * Unified backend interface
 */
class BackendInterface {
    constructor() {
        this.onMessage = null;
        this.onReady = null;
        this.onError = null;
    }

    async start() { throw new Error('Not implemented'); }
    stop() { throw new Error('Not implemented'); }
    sendMidi(data) { throw new Error('Not implemented'); }
    noteOn(note, velocity) { throw new Error('Not implemented'); }
    noteOff(note) { throw new Error('Not implemented'); }
    setParam(id, value) { throw new Error('Not implemented'); }
    getState() { throw new Error('Not implemented'); }
    getAudioContext() { return null; }
    getAnalyzer() { return null; }
    isPlaying() { return false; }
}

/**
 * UMIM Backend - AudioWorklet + WASM (DSP only, NO kernel)
 *
 * Uses synth_wasm.cc -> synth.wasm (PolySynth directly, no kernel).
 * This is the lightweight DSP-only backend.
 */
export class UmimBackend extends BackendInterface {
    constructor(options = {}) {
        super();
        // UMIM uses separate worklet and WASM (no kernel)
        // Path is relative to HTML file location (workbench/)
        this.workletUrl = options.workletUrl || './umim_worklet.js';
        this.wasmUrl = options.wasmUrl || './umim_synth.wasm';
        this.processorName = options.processorName || 'umim-processor';
        this.sampleRate = options.sampleRate || 48000;

        this.audioContext = null;
        this.workletNode = null;
        this.analyzerNode = null;
        this._isPlaying = false;
        this.params = [];
    }

    async start() {
        console.log('[UmimBackend] Starting (using synth.wasm - DSP only, no kernel)...');

        // Fetch WASM binary first
        const wasmResponse = await fetch(this.wasmUrl);
        if (!wasmResponse.ok) {
            throw new Error(`Failed to fetch WASM: ${wasmResponse.status}`);
        }
        const wasmBytes = await wasmResponse.arrayBuffer();
        console.log('[UmimBackend] WASM loaded:', wasmBytes.byteLength, 'bytes');

        // Create audio context
        this.audioContext = new AudioContext({ sampleRate: this.sampleRate });
        console.log('[UmimBackend] AudioContext created:', this.audioContext.sampleRate, 'Hz');

        // Register AudioWorklet (add cache buster)
        const cacheBuster = '?v=' + Date.now();
        await this.audioContext.audioWorklet.addModule(this.workletUrl + cacheBuster);
        console.log('[UmimBackend] Worklet module loaded:', this.workletUrl);

        // Create worklet node
        this.workletNode = new AudioWorkletNode(this.audioContext, this.processorName, {
            outputChannelCount: [1],
            processorOptions: { sampleRate: this.sampleRate }
        });

        // Create analyzer
        this.analyzerNode = this.audioContext.createAnalyser();
        this.analyzerNode.fftSize = 2048;

        // Connect
        this.workletNode.connect(this.analyzerNode);
        this.analyzerNode.connect(this.audioContext.destination);

        // Wait for WASM initialization to complete
        await new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('WASM initialization timeout'));
            }, 10000);

            // Handle messages from worklet
            this.workletNode.port.onmessage = (e) => {
                console.log('[UmimBackend] Worklet message:', e.data.type);
                if (e.data.type === 'ready') {
                    clearTimeout(timeout);
                    this._isPlaying = true;
                    if (this.onReady) this.onReady();
                    resolve();
                } else if (e.data.type === 'params') {
                    this.params = e.data.params;
                } else if (e.data.type === 'error') {
                    clearTimeout(timeout);
                    if (this.onError) this.onError(e.data.message);
                    reject(new Error(e.data.message));
                }
                if (this.onMessage) this.onMessage(e.data);
            };

            // Send WASM to worklet for initialization
            this.workletNode.port.postMessage({ type: 'init', wasmBytes });
        });

        // Set RTC to current time
        const rtcEpoch = Math.floor(Date.now() / 1000);
        this.workletNode.port.postMessage({ type: 'set-rtc', epoch: rtcEpoch });

        console.log('[UmimBackend] Started successfully');
        return true;
    }

    stop() {
        this._isPlaying = false;
        if (this.workletNode) {
            this.workletNode.disconnect();
            this.workletNode = null;
        }
        if (this.analyzerNode) {
            this.analyzerNode.disconnect();
            this.analyzerNode = null;
        }
        if (this.audioContext) {
            this.audioContext.close();
            this.audioContext = null;
        }
    }

    sendMidi(data) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({
                type: 'midi',
                status: data[0],
                data1: data[1] || 0,
                data2: data[2] || 0
            });
        }
    }

    noteOn(note, velocity) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'note-on', note, velocity });
        }
    }

    noteOff(note) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'note-off', note });
        }
    }

    setParam(id, value) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'set-param', id, value });
        }
    }

    getState() {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'get-kernel-state' });
        }
        return null; // State comes via onMessage
    }

    getAudioContext() { return this.audioContext; }
    getAnalyzer() { return this.analyzerNode; }
    isPlaying() { return this._isPlaying; }

    // Additional WASM-specific methods
    sendShellCommand(text) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'shell-input', text });
        }
    }

    applyHwSettings(settings) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'hw-settings', settings });
        }
    }

    resetDspPeak() {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'reset-dsp-peak' });
        }
    }

    reset() {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'reset' });
        }
    }
}

// Legacy alias for backward compatibility
export const WasmBackend = UmimBackend;

/**
 * UMI-OS Backend - Full kernel simulation in WASM
 * Includes task scheduler, MIDI processing, memory management, etc.
 *
 * This uses synth_worklet.js which receives WASM bytes via postMessage,
 * allowing full access to kernel state and hardware simulation.
 */
export class UmiosBackend extends BackendInterface {
    constructor(options = {}) {
        super();
        // Paths relative to HTML file location (workbench/)
        // Use synth/ versions which are the canonical sources
        this.workletUrl = options.workletUrl || '../synth/synth_worklet.js';
        this.wasmUrl = options.wasmUrl || '../synth/synth_sim.wasm';
        this.processorName = options.processorName || 'synth-worklet-processor';
        this.sampleRate = options.sampleRate || 48000;

        this.audioContext = null;
        this.workletNode = null;
        this.analyzerNode = null;
        this._isPlaying = false;
        this.params = [];
    }

    async start() {
        // Fetch WASM binary first
        const wasmResponse = await fetch(this.wasmUrl);
        if (!wasmResponse.ok) {
            throw new Error(`Failed to fetch WASM: ${wasmResponse.status}`);
        }
        const wasmBytes = await wasmResponse.arrayBuffer();
        console.log('[UmiosBackend] WASM loaded:', wasmBytes.byteLength, 'bytes');

        // Create audio context
        this.audioContext = new AudioContext({ sampleRate: this.sampleRate });
        console.log('[UmiosBackend] AudioContext created:', this.audioContext.sampleRate, 'Hz');

        // Register AudioWorklet (add cache buster)
        const cacheBuster = '?v=' + Date.now();
        await this.audioContext.audioWorklet.addModule(this.workletUrl + cacheBuster);
        console.log('[UmiosBackend] Worklet module loaded:', this.workletUrl + cacheBuster);

        // Create worklet node
        this.workletNode = new AudioWorkletNode(this.audioContext, this.processorName, {
            outputChannelCount: [1],
            processorOptions: { sampleRate: this.sampleRate }
        });
        console.log('[UmiosBackend] WorkletNode created');

        // Create analyzer
        this.analyzerNode = this.audioContext.createAnalyser();
        this.analyzerNode.fftSize = 2048;

        // Connect
        this.workletNode.connect(this.analyzerNode);
        this.analyzerNode.connect(this.audioContext.destination);

        // Wait for WASM initialization to complete
        await new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('WASM initialization timeout'));
            }, 10000);

            // Handle messages from worklet
            this.workletNode.port.onmessage = (e) => {
                console.log('[UmiosBackend] Worklet message:', e.data.type);
                if (e.data.type === 'ready') {
                    clearTimeout(timeout);
                    this._isPlaying = true;
                    if (this.onReady) this.onReady();
                    resolve();
                } else if (e.data.type === 'params') {
                    this.params = e.data.params;
                } else if (e.data.type === 'error') {
                    clearTimeout(timeout);
                    if (this.onError) this.onError(e.data.message);
                    reject(new Error(e.data.message));
                }
                if (this.onMessage) this.onMessage(e.data);
            };

            // Send WASM to worklet for initialization
            this.workletNode.port.postMessage({ type: 'init', wasmBytes });
        });

        // Set RTC to current time
        const rtcEpoch = Math.floor(Date.now() / 1000);
        this.workletNode.port.postMessage({ type: 'set-rtc', epoch: rtcEpoch });

        console.log('[UmiosBackend] Started successfully');
        return true;
    }

    stop() {
        this._isPlaying = false;
        if (this.workletNode) {
            this.workletNode.disconnect();
            this.workletNode = null;
        }
        if (this.analyzerNode) {
            this.analyzerNode.disconnect();
            this.analyzerNode = null;
        }
        if (this.audioContext) {
            this.audioContext.close();
            this.audioContext = null;
        }
    }

    sendMidi(data) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({
                type: 'midi',
                status: data[0],
                data1: data[1] || 0,
                data2: data[2] || 0
            });
        }
    }

    noteOn(note, velocity) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'note-on', note, velocity });
        }
    }

    noteOff(note) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'note-off', note });
        }
    }

    setParam(id, value) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'set-param', id, value });
        }
    }

    getState() {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'get-kernel-state' });
        }
        return null; // State comes via onMessage
    }

    getAudioContext() { return this.audioContext; }
    getAnalyzer() { return this.analyzerNode; }
    isPlaying() { return this._isPlaying; }

    // Additional UMI-OS specific methods
    sendShellCommand(text) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'shell-input', text });
        }
    }

    applyHwSettings(settings) {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'hw-settings', settings });
        }
    }

    resetDspPeak() {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'reset-dsp-peak' });
        }
    }

    reset() {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'reset' });
        }
    }
}

/**
 * Renode Backend - WebSocket to Renode Bridge
 */
export class RenodeBackend extends BackendInterface {
    constructor(options = {}) {
        super();
        this.wsUrl = options.wsUrl || 'ws://localhost:8089';
        this.adapter = new RenodeAdapter(this.wsUrl);
        this.analyzerNode = null;
    }

    async start() {
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
        this.adapter.stopAudio();
        this.adapter.disconnect();
    }

    sendMidi(data) {
        this.adapter.sendMidi(data);
    }

    noteOn(note, velocity) {
        this.adapter.noteOn(note, velocity);
    }

    noteOff(note) {
        this.adapter.noteOff(note);
    }

    setParam(id, value) {
        this.adapter.setParam(id, value);
    }

    getState() {
        return this.adapter.getKernelState();
    }

    getAudioContext() { return this.adapter.getAudioContext(); }
    getAnalyzer() { return this.analyzerNode; }
    isPlaying() { return this.adapter.isPlaying; }
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
     */
    static async isAvailable() {
        // Check for rp2040js or thumbulator
        try {
            // Try to import dynamically
            // In a real implementation, you would check if the library is loaded
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
        // Full implementation would:
        // 1. Initialize CPU emulator
        // 2. Load ELF binary
        // 3. Set up memory regions
        // 4. Configure peripherals
        // 5. Start execution loop

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

/**
 * Backend Manager - Factory and coordinator
 */
export class BackendManager {
    constructor() {
        this.currentBackend = null;
        this.currentType = null;
    }

    /**
     * Get available backends
     */
    async getAvailableBackends() {
        const backends = [
            {
                type: BackendType.UMIM,
                name: 'UMIM (DSP)',
                description: 'DSP layer only, fast real-time audio',
                available: true,
            },
            {
                type: BackendType.UMIOS,
                name: 'UMI-OS (Kernel)',
                description: 'Full UMI-OS kernel simulation with task scheduler',
                available: true,
            },
            {
                type: BackendType.RENODE,
                name: 'Renode (HW)',
                description: 'Cycle-accurate hardware simulation, requires Renode server',
                available: false, // Will be checked
            },
        ];

        // Check Renode availability
        try {
            const ws = new WebSocket('ws://localhost:8089');
            await new Promise((resolve, reject) => {
                ws.onopen = () => { ws.close(); resolve(); };
                ws.onerror = reject;
                setTimeout(reject, 1000);
            });
            backends[2].available = true;
        } catch {
            backends[2].available = false;
        }

        return backends;
    }

    /**
     * Create backend of specified type
     */
    createBackend(type, options = {}) {
        // Resolve type string to actual backend
        // BackendType.WASM = 'umios', BackendType.UMIM = 'umim', BackendType.UMIOS = 'umios'
        switch (type) {
            case 'umim':
                return new UmimBackend(options);
            case 'umios':
                return new UmiosBackend(options);
            case 'renode':
                return new RenodeBackend(options);
            default:
                throw new Error(`Unknown backend type: ${type}`);
        }
    }

    /**
     * Switch to a different backend
     */
    async switchBackend(type, options = {}) {
        // Stop current backend
        if (this.currentBackend) {
            this.currentBackend.stop();
            this.currentBackend = null;
        }

        // Create new backend
        this.currentBackend = this.createBackend(type, options);
        this.currentType = type;

        return this.currentBackend;
    }

    /**
     * Get current backend
     */
    getBackend() {
        return this.currentBackend;
    }

    /**
     * Get current backend type
     */
    getType() {
        return this.currentType;
    }
}

// Export singleton instance
export const backendManager = new BackendManager();

// Export for use in HTML
if (typeof window !== 'undefined') {
    window.BackendManager = BackendManager;
    window.BackendType = BackendType;
    window.UmimBackend = UmimBackend;
    window.UmiosBackend = UmiosBackend;
    window.WasmBackend = WasmBackend;  // Legacy alias
    window.RenodeBackend = RenodeBackend;
    window.backendManager = backendManager;
}
