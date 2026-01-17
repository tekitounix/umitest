/**
 * UMI-OS Web Simulation - AudioWorklet Processor
 *
 * This runs in the AudioWorklet thread and processes audio using
 * the compiled WASM module.
 */

class UmiSimProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();

        this.wasmInstance = null;
        this.wasmMemory = null;
        this.wasmReady = false;
        this.sampleRate = options.processorOptions?.sampleRate || sampleRate;

        // Allocate buffer pointers (will be set after WASM init)
        this.inputPtr = 0;
        this.outputPtr = 0;
        this.bufferSize = 128;

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
                    this.wasmInstance.exports.umi_sim_note_on(msg.note, msg.velocity);
                }
                break;
            case 'note-off':
                if (this.wasmReady) {
                    this.wasmInstance.exports.umi_sim_note_off(msg.note);
                }
                break;
            case 'cc':
                if (this.wasmReady) {
                    this.wasmInstance.exports.umi_sim_cc(msg.cc, msg.value);
                }
                break;
            case 'midi':
                if (this.wasmReady) {
                    this.wasmInstance.exports.umi_sim_midi(msg.status, msg.data1, msg.data2);
                }
                break;
            case 'reset':
                if (this.wasmReady) {
                    this.wasmInstance.exports.umi_sim_reset();
                }
                break;
        }
    }

    async initWasm(wasmModule, wasmMemory) {
        try {
            // Create import object for WASM
            const importObject = {
                env: {
                    memory: wasmMemory || new WebAssembly.Memory({ initial: 256 }),
                    emscripten_notify_memory_growth: () => {},
                    // Emscripten runtime stubs
                    __cxa_throw: () => { throw new Error('C++ exception'); },
                    abort: () => { throw new Error('WASM abort'); },
                },
                wasi_snapshot_preview1: {
                    proc_exit: () => {},
                    fd_close: () => 0,
                    fd_write: () => 0,
                    fd_seek: () => 0,
                    fd_read: () => 0,
                    environ_get: () => 0,
                    environ_sizes_get: () => 0,
                    clock_time_get: () => 0,
                },
            };

            // Instantiate WASM module
            const result = await WebAssembly.instantiate(wasmModule, importObject);
            this.wasmInstance = result;
            this.wasmMemory = result.exports.memory || wasmMemory;

            // Initialize the simulation
            if (this.wasmInstance.exports.umi_sim_init) {
                this.wasmInstance.exports.umi_sim_init();
            } else if (this.wasmInstance.exports.umi_create) {
                this.wasmInstance.exports.umi_create();
            }

            // Allocate buffers in WASM memory
            this.allocateBuffers();

            this.wasmReady = true;
            this.port.postMessage({ type: 'ready' });

        } catch (err) {
            console.error('[UmiSimProcessor] WASM init failed:', err);
            this.port.postMessage({ type: 'error', message: err.message });
        }
    }

    allocateBuffers() {
        // Use a fixed region in WASM memory for I/O buffers
        // This avoids malloc/free complexity
        const heapBase = 65536; // Start after the first 64KB
        this.inputPtr = heapBase;
        this.outputPtr = heapBase + this.bufferSize * 4;
    }

    process(inputs, outputs, parameters) {
        const output = outputs[0];
        if (!output || output.length === 0) return true;

        const frames = output[0].length;

        if (!this.wasmReady) {
            // Output silence while initializing
            for (const channel of output) {
                channel.fill(0);
            }
            return true;
        }

        try {
            const exports = this.wasmInstance.exports;
            const heap = new Float32Array(this.wasmMemory.buffer);

            // Get input (if available)
            const input = inputs[0];
            if (input && input[0]) {
                heap.set(input[0], this.inputPtr / 4);
            }

            // Call the process function
            if (exports.umi_sim_process) {
                exports.umi_sim_process(
                    this.inputPtr,
                    this.outputPtr,
                    frames,
                    Math.round(this.sampleRate)
                );
            } else if (exports.umi_process) {
                exports.umi_process(
                    this.inputPtr,
                    this.outputPtr,
                    frames,
                    Math.round(this.sampleRate)
                );
            }

            // Copy output from WASM memory
            const outputStart = this.outputPtr / 4;
            for (const channel of output) {
                channel.set(heap.subarray(outputStart, outputStart + frames));
            }

        } catch (err) {
            console.error('[UmiSimProcessor] Process error:', err);
            // Output silence on error
            for (const channel of output) {
                channel.fill(0);
            }
        }

        return true;
    }
}

registerProcessor('umi-sim-processor', UmiSimProcessor);
