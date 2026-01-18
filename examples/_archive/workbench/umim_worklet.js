/**
 * UMIM AudioWorklet Processor
 *
 * DSP-only processor using synth_wasm.cc (PolySynth directly, no kernel).
 * Uses umi_* API (not umi_sim_* which includes kernel).
 */

class UmimProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();

        this.wasmInstance = null;
        this.wasmMemory = null;
        this.wasmReady = false;
        this.sampleRate = options.processorOptions?.sampleRate || sampleRate;

        // Buffer pointers
        this.inputPtr = 0;
        this.outputPtr = 0;
        this.bufferSize = 128;

        // Malloc function (if available)
        this.malloc = null;

        // Message handling
        this.port.onmessage = (e) => this.handleMessage(e.data);
    }

    handleMessage(msg) {
        switch (msg.type) {
            case 'init':
                this.initWasm(msg.wasmBytes);
                break;
            case 'note-on':
                if (this.wasmReady && this.fn.noteOn) {
                    this.fn.noteOn(msg.note, msg.velocity);
                }
                break;
            case 'note-off':
                if (this.wasmReady && this.fn.noteOff) {
                    this.fn.noteOff(msg.note);
                }
                break;
            case 'set-param':
                if (this.wasmReady && this.fn.setParam) {
                    this.fn.setParam(msg.id, msg.value);
                }
                break;
            case 'midi':
                // Handle raw MIDI if needed
                if (this.wasmReady) {
                    const status = msg.status;
                    const data1 = msg.data1;
                    const data2 = msg.data2;
                    const cmd = status & 0xF0;
                    if (cmd === 0x90 && data2 > 0) {
                        this.fn.noteOn(data1, data2);
                    } else if (cmd === 0x80 || (cmd === 0x90 && data2 === 0)) {
                        this.fn.noteOff(data1);
                    }
                }
                break;
        }
    }

    async initWasm(wasmBytes) {
        try {
            console.log('[UmimProcessor] Initializing WASM...');

            // Create import object
            const importObject = {
                env: {
                    memory: new WebAssembly.Memory({ initial: 256 }),
                    emscripten_notify_memory_growth: () => {},
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
                a: {
                    a: () => {},  // Emscripten stub
                }
            };

            // Instantiate
            const result = await WebAssembly.instantiate(wasmBytes, importObject);
            this.wasmInstance = result.instance;
            this.wasmMemory = result.instance.exports.memory || result.instance.exports.b;

            const exports = this.wasmInstance.exports;
            console.log('[UmimProcessor] WASM exports:', Object.keys(exports).join(', '));

            // Initialize Emscripten runtime if available
            // c = __wasm_call_ctors
            const wasmCallCtors = exports.c || exports.__wasm_call_ctors;
            if (wasmCallCtors) {
                wasmCallCtors();
                console.log('[UmimProcessor] Emscripten runtime initialized');
            }

            // Map function names based on synth.js export mappings:
            // d=umi_create, o=umi_process, q=umi_note_on, r=umi_note_off, s=umi_set_param, I=malloc
            this.fn = {
                create: exports.d || exports.umi_create,
                process: exports.o || exports.umi_process,
                noteOn: exports.q || exports.umi_note_on,
                noteOff: exports.r || exports.umi_note_off,
                setParam: exports.s || exports.umi_set_param,
            };

            // Get malloc for buffer allocation (I = malloc)
            this.malloc = exports.I || exports.malloc || exports._malloc;

            // Initialize synth
            if (this.fn.create) {
                this.fn.create();
                console.log('[UmimProcessor] umi_create called');
            }

            // Allocate buffers
            this.allocateBuffers();

            this.wasmReady = true;
            this.port.postMessage({ type: 'ready' });
            console.log('[UmimProcessor] Ready');

        } catch (err) {
            console.error('[UmimProcessor] WASM init failed:', err);
            this.port.postMessage({ type: 'error', message: err.message });
        }
    }

    allocateBuffers() {
        const size = this.bufferSize * 4;  // float32 = 4 bytes

        if (this.malloc) {
            this.inputPtr = this.malloc(size);
            this.outputPtr = this.malloc(size);
            console.log('[UmimProcessor] Buffers allocated via malloc:', this.inputPtr, this.outputPtr);
        } else {
            // Fallback: use fixed memory region
            const heapBase = 65536;
            this.inputPtr = heapBase;
            this.outputPtr = heapBase + size;
            console.log('[UmimProcessor] Buffers at fixed addresses:', this.inputPtr, this.outputPtr);
        }
    }

    process(inputs, outputs, parameters) {
        const output = outputs[0];
        if (!output || output.length === 0) return true;

        const channel = output[0];
        const frames = channel.length;

        if (!this.wasmReady || !this.fn.process) {
            channel.fill(0);
            return true;
        }

        try {
            // Get WASM heap (may have been resized)
            const heap = new Float32Array(this.wasmMemory.buffer);

            // Process audio with sample rate
            // umi_process(input, output, frames, sample_rate)
            this.fn.process(
                this.inputPtr,
                this.outputPtr,
                frames,
                Math.round(sampleRate)  // Use AudioWorklet's sampleRate global
            );

            // Copy output
            const start = this.outputPtr / 4;
            channel.set(heap.subarray(start, start + frames));

            // Copy to other channels if stereo
            for (let ch = 1; ch < output.length; ch++) {
                output[ch].set(channel);
            }

        } catch (err) {
            console.error('[UmimProcessor] Process error:', err);
            channel.fill(0);
        }

        return true;
    }
}

registerProcessor('umim-processor', UmimProcessor);
