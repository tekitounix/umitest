/**
 * UMI Web - UMI-OS Backends
 *
 * UMI-OS backends for full kernel simulation.
 * Includes task scheduler, MIDI processing, memory management, etc.
 *
 * @module umi_web/core/backends/umios
 */

import { BackendInterface } from '../backend.js';

/**
 * UMI-OS Backend - Full kernel simulation in WASM
 *
 * Includes task scheduler, MIDI processing, memory management, etc.
 * Uses synth_sim_worklet.js which receives WASM bytes via postMessage.
 */
export class UmiosBackend extends BackendInterface {
    /**
     * @param {object} options
     * @param {string} [options.workletUrl='./synth_sim_worklet.js']
     * @param {string} [options.wasmUrl='./webhost_sim.wasm']
     * @param {string} [options.processorName='synth-worklet-processor']
     * @param {number} [options.sampleRate=48000]
     */
    constructor(options = {}) {
        super();
        this.workletUrl = options.workletUrl || './synth_sim_worklet.js';
        this.wasmUrl = options.wasmUrl || './webhost_sim.wasm';
        this.processorName = options.processorName || 'synth-worklet-processor';
        this.sampleRate = options.sampleRate || 48000;

        this.audioContext = null;
        this.workletNode = null;
        this.analyzerNode = null;
        this._isPlaying = false;
        this.params = [];
    }

    async start() {
        // Fetch WASM binary first (with cache buster)
        const wasmCacheBuster = '?v=' + Date.now();
        const wasmResponse = await fetch(this.wasmUrl + wasmCacheBuster);
        if (!wasmResponse.ok) {
            throw new Error(`Failed to fetch WASM: ${wasmResponse.status}`);
        }
        const wasmBytes = await wasmResponse.arrayBuffer();
        console.log('[UmiosBackend] WASM loaded:', wasmBytes.byteLength, 'bytes from', this.wasmUrl + wasmCacheBuster);

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

    // UMI-OS specific methods
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
 * Generic UMI-OS Backend - Dynamic WASM loading for UMI-OS apps
 *
 * Uses synth_sim_worklet.js (or compatible) which can load any UMI-OS WASM
 * via postMessage. Includes full kernel simulation support.
 */
export class UmiosGenericBackend extends BackendInterface {
    /**
     * @param {object} options
     * @param {string} [options.workletUrl='./synth_sim_worklet.js']
     * @param {string} options.wasmUrl - Required: path to WASM file
     * @param {string} [options.processorName='synth-worklet-processor']
     * @param {number} [options.sampleRate=48000]
     * @param {object} [options.appInfo] - Application info { name, vendor, version }
     */
    constructor(options = {}) {
        super();
        this.workletUrl = options.workletUrl || './synth_sim_worklet.js';
        this.wasmUrl = options.wasmUrl;  // Required - path to WASM
        this.processorName = options.processorName || 'synth-worklet-processor';
        this.sampleRate = options.sampleRate || 48000;
        this.appInfo = options.appInfo || { name: 'Unknown', vendor: 'Unknown', version: '0.0.0' };

        this.audioContext = null;
        this.workletNode = null;
        this.analyzerNode = null;
        this._isPlaying = false;
        this.params = [];
    }

    async start() {
        if (!this.wasmUrl) {
            throw new Error('wasmUrl is required for UmiosGenericBackend');
        }

        // Fetch WASM binary
        const wasmCacheBuster = '?v=' + Date.now();
        const wasmResponse = await fetch(this.wasmUrl + wasmCacheBuster);
        if (!wasmResponse.ok) {
            throw new Error(`Failed to fetch WASM: ${wasmResponse.status} from ${this.wasmUrl}`);
        }
        const wasmBytes = await wasmResponse.arrayBuffer();
        console.log('[UmiosGenericBackend] WASM loaded:', wasmBytes.byteLength, 'bytes from', this.wasmUrl);

        // Create audio context
        this.audioContext = new AudioContext({ sampleRate: this.sampleRate });
        console.log('[UmiosGenericBackend] AudioContext created:', this.audioContext.sampleRate, 'Hz');

        // Register AudioWorklet
        const cacheBuster = '?v=' + Date.now();
        await this.audioContext.audioWorklet.addModule(this.workletUrl + cacheBuster);
        console.log('[UmiosGenericBackend] Worklet module loaded:', this.workletUrl);

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

        // Wait for WASM initialization
        await new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                reject(new Error('WASM initialization timeout'));
            }, 10000);

            this.workletNode.port.onmessage = (e) => {
                console.log('[UmiosGenericBackend] Worklet message:', e.data.type);
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

            // Send WASM to worklet
            this.workletNode.port.postMessage({ type: 'init', wasmBytes });
        });

        // Set RTC to current time
        const rtcEpoch = Math.floor(Date.now() / 1000);
        this.workletNode.port.postMessage({ type: 'set-rtc', epoch: rtcEpoch });

        console.log(`[UmiosGenericBackend] Started: ${this.appInfo.name} v${this.appInfo.version}`);
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
        return null;
    }

    getAudioContext() { return this.audioContext; }
    getAnalyzer() { return this.analyzerNode; }
    isPlaying() { return this._isPlaying; }
    getAppInfo() { return this.appInfo; }

    // UMI-OS specific methods
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
