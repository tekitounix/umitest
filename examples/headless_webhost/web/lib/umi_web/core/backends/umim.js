/**
 * UMI Web - UMIM Backends
 *
 * UMIM (UMI Module) backends for DSP-only processing.
 * These backends use AudioWorklet + WASM without kernel simulation.
 *
 * @module umi_web/core/backends/umim
 */

import { BackendInterface } from '../backend.js';

/**
 * UMIM Backend - AudioWorklet + WASM (DSP only, NO kernel)
 *
 * Uses embedded WASM in the worklet for minimal latency.
 * This is the lightweight DSP-only backend.
 */
export class UmimBackend extends BackendInterface {
    /**
     * @param {object} options
     * @param {string} [options.workletUrl='./umim_generic_worklet.js']
     * @param {string} [options.wasmUrl='./umim_synth.wasm']
     * @param {string} [options.processorName='umim-generic-processor']
     * @param {number} [options.sampleRate=48000]
     */
    constructor(options = {}) {
        super();
        this.workletUrl = options.workletUrl || './umim_generic_worklet.js';
        this.wasmUrl = options.wasmUrl || './umim_synth.wasm';
        this.processorName = options.processorName || 'umim-generic-processor';
        this.sampleRate = options.sampleRate || 48000;

        this.audioContext = null;
        this.workletNode = null;
        this.analyzerNode = null;
        this._isPlaying = false;
        this.params = [];
    }

    async start() {
        console.log('[UmimBackend] Starting (using embedded WASM - DSP only, no kernel)...');

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

            // Send init-embedded message to use embedded WASM
            this.workletNode.port.postMessage({ type: 'init-embedded' });
        });

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

/**
 * Generic UMIM Backend - Dynamic WASM loading
 *
 * Uses umim_generic_worklet.js which can load any UMIM-compatible WASM
 * via postMessage (no embedded WASM).
 */
export class UmimGenericBackend extends BackendInterface {
    /**
     * @param {object} options
     * @param {string} [options.workletUrl='./umim_generic_worklet.js']
     * @param {string} options.wasmUrl - Required: path to WASM file
     * @param {string} [options.processorName='umim-generic-processor']
     * @param {number} [options.sampleRate=48000]
     */
    constructor(options = {}) {
        super();
        this.workletUrl = options.workletUrl || './umim_generic_worklet.js';
        this.wasmUrl = options.wasmUrl;  // Required - path to WASM
        this.processorName = options.processorName || 'umim-generic-processor';
        this.sampleRate = options.sampleRate || 48000;

        this.audioContext = null;
        this.workletNode = null;
        this.analyzerNode = null;
        this._isPlaying = false;
        this.params = [];
        this.appInfo = { name: 'Unknown', vendor: 'Unknown', version: '0.0.0' };
    }

    async start() {
        if (!this.wasmUrl) {
            throw new Error('wasmUrl is required for UmimGenericBackend');
        }

        // Fetch WASM binary
        const wasmCacheBuster = '?v=' + Date.now();
        const wasmResponse = await fetch(this.wasmUrl + wasmCacheBuster);
        if (!wasmResponse.ok) {
            throw new Error(`Failed to fetch WASM: ${wasmResponse.status} from ${this.wasmUrl}`);
        }
        const wasmBytes = await wasmResponse.arrayBuffer();
        console.log('[UmimGenericBackend] WASM loaded:', wasmBytes.byteLength, 'bytes from', this.wasmUrl);

        // Create audio context
        this.audioContext = new AudioContext({ sampleRate: this.sampleRate });
        console.log('[UmimGenericBackend] AudioContext created:', this.audioContext.sampleRate, 'Hz');

        // Register AudioWorklet
        const cacheBuster = '?v=' + Date.now();
        await this.audioContext.audioWorklet.addModule(this.workletUrl + cacheBuster);
        console.log('[UmimGenericBackend] Worklet module loaded:', this.workletUrl);

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
                console.log('[UmimGenericBackend] Worklet message:', e.data.type);
                if (e.data.type === 'ready') {
                    clearTimeout(timeout);
                    this._isPlaying = true;
                    this.appInfo = {
                        name: e.data.name || 'Unknown',
                        vendor: e.data.vendor || 'Unknown',
                        version: e.data.version || '0.0.0'
                    };
                    this.params = e.data.params || [];
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

        console.log(`[UmimGenericBackend] Started: ${this.appInfo.name} v${this.appInfo.version} by ${this.appInfo.vendor}`);
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
            this.workletNode.port.postMessage({ type: 'midi', data: Array.from(data) });
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
            this.workletNode.port.postMessage({ type: 'set-param', index: id, value });
        }
    }

    getState() {
        if (this.workletNode) {
            this.workletNode.port.postMessage({ type: 'get-params' });
        }
        return null;
    }

    getAudioContext() { return this.audioContext; }
    getAnalyzer() { return this.analyzerNode; }
    isPlaying() { return this._isPlaying; }
    getAppInfo() { return this.appInfo; }
}

// Legacy alias
export const WasmBackend = UmimBackend;
