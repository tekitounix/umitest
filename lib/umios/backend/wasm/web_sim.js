/**
 * UMI-OS Web Simulation Runtime
 *
 * Provides WebAudio and WebMIDI integration for running embedded
 * UMI-OS applications in browser.
 *
 * Usage:
 *   const sim = new UmiSim('synth_sim.js');
 *   await sim.init();
 *   sim.noteOn(60, 100);
 */

class UmiSimProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();

        this.wasm = null;
        this.wasmReady = false;
        this.sampleRate = options.processorOptions?.sampleRate || 48000;

        // Message handling
        this.port.onmessage = (e) => this.handleMessage(e.data);

        // Request WASM module from main thread
        this.port.postMessage({ type: 'request-wasm' });
    }

    handleMessage(msg) {
        switch (msg.type) {
            case 'wasm-module':
                this.initWasm(msg.wasmModule, msg.wasmMemory);
                break;
            case 'note-on':
                if (this.wasmReady) {
                    this.wasm.instance.exports.umi_sim_note_on(msg.note, msg.velocity);
                }
                break;
            case 'note-off':
                if (this.wasmReady) {
                    this.wasm.instance.exports.umi_sim_note_off(msg.note);
                }
                break;
            case 'cc':
                if (this.wasmReady) {
                    this.wasm.instance.exports.umi_sim_cc(msg.cc, msg.value);
                }
                break;
            case 'midi':
                if (this.wasmReady) {
                    this.wasm.instance.exports.umi_sim_midi(msg.status, msg.data1, msg.data2);
                }
                break;
            case 'reset':
                if (this.wasmReady) {
                    this.wasm.instance.exports.umi_sim_reset();
                }
                break;
        }
    }

    async initWasm(wasmModule, wasmMemory) {
        try {
            const importObject = {
                env: {
                    memory: wasmMemory,
                    emscripten_notify_memory_growth: () => {},
                },
                wasi_snapshot_preview1: {
                    proc_exit: () => {},
                    fd_close: () => 0,
                    fd_write: () => 0,
                    fd_seek: () => 0,
                },
            };

            this.wasm = await WebAssembly.instantiate(wasmModule, importObject);
            this.wasm.instance.exports.umi_sim_init();
            this.wasmReady = true;

            this.port.postMessage({ type: 'ready' });
        } catch (err) {
            console.error('WASM init failed:', err);
            this.port.postMessage({ type: 'error', message: err.message });
        }
    }

    process(inputs, outputs, parameters) {
        if (!this.wasmReady) {
            // Output silence while initializing
            for (const output of outputs) {
                for (const channel of output) {
                    channel.fill(0);
                }
            }
            return true;
        }

        const output = outputs[0];
        const frames = output[0].length;
        const exports = this.wasm.instance.exports;

        // Allocate WASM memory for output buffer
        const outPtr = exports.malloc ? exports.malloc(frames * 4) : 0;

        if (outPtr) {
            // Process audio
            exports.umi_sim_process(0, outPtr, frames, sampleRate);

            // Copy to output
            const heap = new Float32Array(exports.memory.buffer);
            const startIdx = outPtr / 4;
            for (let ch = 0; ch < output.length; ch++) {
                output[ch].set(heap.subarray(startIdx, startIdx + frames));
            }

            exports.free(outPtr);
        } else {
            // Fallback: process sample by sample (slower)
            const heap = new Float32Array(exports.memory.buffer);
            const tempPtr = 1024; // Use fixed offset in WASM memory

            exports.umi_sim_process(0, tempPtr, frames, sampleRate);

            for (let ch = 0; ch < output.length; ch++) {
                for (let i = 0; i < frames; i++) {
                    output[ch][i] = heap[tempPtr / 4 + i];
                }
            }
        }

        return true;
    }
}

registerProcessor('umi-sim-processor', UmiSimProcessor);


/**
 * Main thread runtime for UMI-OS Web Simulation
 */
class UmiSim {
    constructor(wasmUrl) {
        this.wasmUrl = wasmUrl;
        this.audioContext = null;
        this.workletNode = null;
        this.wasmModule = null;
        this.wasmMemory = null;
        this.onReady = null;
        this.midiAccess = null;
    }

    /**
     * Initialize the simulation
     * @param {Object} options - Configuration options
     * @param {number} options.sampleRate - Audio sample rate (default: 48000)
     * @param {boolean} options.enableMidi - Enable WebMIDI input (default: true)
     */
    async init(options = {}) {
        const sampleRate = options.sampleRate || 48000;
        const enableMidi = options.enableMidi !== false;

        // Create audio context
        this.audioContext = new AudioContext({ sampleRate });

        // Load WASM module
        const wasmResponse = await fetch(this.wasmUrl.replace('.js', '.wasm'));
        const wasmBytes = await wasmResponse.arrayBuffer();
        this.wasmModule = await WebAssembly.compile(wasmBytes);

        // Create shared memory
        this.wasmMemory = new WebAssembly.Memory({
            initial: 256,
            maximum: 512,
            shared: true,
        });

        // Load AudioWorklet
        const workletUrl = new URL('web_sim_worklet.js', import.meta.url);
        await this.audioContext.audioWorklet.addModule(workletUrl);

        // Create worklet node
        this.workletNode = new AudioWorkletNode(
            this.audioContext,
            'umi-sim-processor',
            {
                processorOptions: { sampleRate },
                outputChannelCount: [1],
            }
        );

        // Handle messages from worklet
        this.workletNode.port.onmessage = (e) => {
            if (e.data.type === 'request-wasm') {
                this.workletNode.port.postMessage({
                    type: 'wasm-module',
                    wasmModule: this.wasmModule,
                    wasmMemory: this.wasmMemory,
                });
            } else if (e.data.type === 'ready') {
                if (this.onReady) this.onReady();
            }
        };

        // Connect to output
        this.workletNode.connect(this.audioContext.destination);

        // Initialize WebMIDI
        if (enableMidi && navigator.requestMIDIAccess) {
            try {
                this.midiAccess = await navigator.requestMIDIAccess();
                this.setupMidiInputs();
            } catch (err) {
                console.warn('WebMIDI not available:', err);
            }
        }

        return this;
    }

    /**
     * Start audio playback
     */
    async start() {
        if (this.audioContext.state === 'suspended') {
            await this.audioContext.resume();
        }
    }

    /**
     * Stop audio playback
     */
    async stop() {
        if (this.audioContext.state === 'running') {
            await this.audioContext.suspend();
        }
    }

    /**
     * Send Note On
     */
    noteOn(note, velocity = 100) {
        this.workletNode?.port.postMessage({
            type: 'note-on',
            note,
            velocity,
        });
    }

    /**
     * Send Note Off
     */
    noteOff(note) {
        this.workletNode?.port.postMessage({
            type: 'note-off',
            note,
        });
    }

    /**
     * Send Control Change
     */
    cc(controller, value) {
        this.workletNode?.port.postMessage({
            type: 'cc',
            cc: controller,
            value,
        });
    }

    /**
     * Send raw MIDI message
     */
    midi(status, data1, data2 = 0) {
        this.workletNode?.port.postMessage({
            type: 'midi',
            status,
            data1,
            data2,
        });
    }

    /**
     * Reset the simulation
     */
    reset() {
        this.workletNode?.port.postMessage({ type: 'reset' });
    }

    /**
     * Setup WebMIDI input handling
     */
    setupMidiInputs() {
        if (!this.midiAccess) return;

        for (const input of this.midiAccess.inputs.values()) {
            input.onmidimessage = (e) => {
                if (e.data.length >= 2) {
                    this.midi(e.data[0], e.data[1], e.data[2] || 0);
                }
            };
        }

        // Listen for new devices
        this.midiAccess.onstatechange = () => {
            this.setupMidiInputs();
        };
    }

    /**
     * Get audio context (for visualization, etc.)
     */
    getAudioContext() {
        return this.audioContext;
    }

    /**
     * Connect to an analyzer node
     */
    connectAnalyzer(analyzerNode) {
        this.workletNode?.connect(analyzerNode);
    }
}

// Export for use in modules
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { UmiSim };
}
