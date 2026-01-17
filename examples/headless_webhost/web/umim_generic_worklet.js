/**
 * Generic UMIM AudioWorklet Processor
 *
 * Loads WASM binary dynamically via message from main thread.
 * Supports any UMIM-compatible application.
 * Also supports embedded WASM fallback for offline use.
 *
 * UMIM API expected exports:
 *   umi_create(), umi_destroy(), umi_process(input, output, frames, sampleRate)
 *   umi_note_on(note, velocity), umi_note_off(note)
 *   umi_set_param(index, value), umi_get_param(index)
 *   umi_get_param_count(), umi_get_param_name(index)
 *   umi_get_name(), umi_get_vendor(), umi_get_version()
 */

// Embedded WASM binary (base64) - built from umim_synth.cc
// To rebuild: xmake build umim_synth && base64 -i build/umim_synth.wasm
const EMBEDDED_WASM_BASE64 = "AGFzbQEAAAABSw5gAAF/YAF/AX9gAABgAX8AYAJ/fwF/YAJ/fwBgAX8BfWAEf39/fwBgA39/fwBgAXwBfWAEf39/fwF/YAJ/fQBgAX0BfWACfH8BfAKFAQQWd2FzaV9zbmFwc2hvdF9wcmV2aWV3MQ5hcmdzX3NpemVzX2dldAAEFndhc2lfc25hcHNob3RfcHJldmlldzEIYXJnc19nZXQABANlbnYQX19tYWluX2FyZ2NfYXJndgAEFndhc2lfc25hcHNob3RfcHJldmlldzEJcHJvY19leGl0AAMDKSgCAgIAAQEBBQEHCgIHBQYIBQMLBgABBgEIAAAAAAACDAkJDQEBAwMABAUBcAECAgUEAQEQEAYIAX8BQaCwBAsHwAUmBm1lbW9yeQIACnVtaV9jcmVhdGUABQt1bWlfZGVzdHJveQAGEnVtaV9nZXRfcG9ydF9jb3VudAAHEXVtaV9nZXRfcG9ydF9uYW1lAAgWdW1pX2dldF9wb3J0X2RpcmVjdGlvbgAJEXVtaV9nZXRfcG9ydF9raW5kAAoTdW1pX3NldF9wb3J0X2J1ZmZlcgALE3VtaV9nZXRfcG9ydF9idWZmZXIADA51bWlfc2VuZF9ldmVudAANDnVtaV9yZWN2X2V2ZW50AA4QdW1pX2NsZWFyX2V2ZW50cwAPC3VtaV9wcm9jZXNzABASdW1pX3Byb2Nlc3Nfc2ltcGxlABMLdW1pX25vdGVfb24AFAx1bWlfbm90ZV9vZmYAFQ11bWlfc2V0X3BhcmFtABYNdW1pX2dldF9wYXJhbQAXE3VtaV9nZXRfcGFyYW1fY291bnQAGBJ1bWlfZ2V0X3BhcmFtX25hbWUAGRF1bWlfZ2V0X3BhcmFtX21pbgAXEXVtaV9nZXRfcGFyYW1fbWF4ABoVdW1pX2dldF9wYXJhbV9kZWZhdWx0ABcTdW1pX2dldF9wYXJhbV9jdXJ2ZQAbEHVtaV9nZXRfcGFyYW1faWQAGxJ1bWlfZ2V0X3BhcmFtX3VuaXQAGQ51bWlfcHJvY2Vzc19jYwAcFnVtaV9nZXRfcHJvY2Vzc29yX25hbWUAHQx1bWlfZ2V0X25hbWUAHg51bWlfZ2V0X3ZlbmRvcgAfD3VtaV9nZXRfdmVyc2lvbgAgDHVtaV9nZXRfdHlwZQAhGV9faW5kaXJlY3RfZnVuY3Rp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";

// Decode base64 to ArrayBuffer
function base64ToArrayBuffer(base64) {
    const binaryString = atob(base64);
    const bytes = new Uint8Array(binaryString.length);
    for (let i = 0; i < binaryString.length; i++) {
        bytes[i] = binaryString.charCodeAt(i);
    }
    return bytes.buffer;
}

// atob polyfill for AudioWorkletGlobalScope
if (typeof atob === "undefined") {
    var atob = function(base64) {
        const chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        let result = "";
        base64 = base64.replace(/=+$/, "");
        for (let i = 0; i < base64.length; i += 4) {
            const a = chars.indexOf(base64[i]);
            const b = chars.indexOf(base64[i + 1]);
            const c = chars.indexOf(base64[i + 2]);
            const d = chars.indexOf(base64[i + 3]);
            result += String.fromCharCode((a << 2) | (b >> 4));
            if (base64[i + 2] !== undefined && base64[i + 2] !== '=')
                result += String.fromCharCode(((b & 15) << 4) | (c >> 2));
            if (base64[i + 3] !== undefined && base64[i + 3] !== '=')
                result += String.fromCharCode(((c & 3) << 6) | d);
        }
        return result;
    };
}

/**
 * Generic UMIM AudioWorklet Processor
 */
class UmimGenericProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();
        this.ready = false;
        this.wasmExports = null;
        this.wasmMemory = null;

        // Fixed buffer addresses in WASM memory
        this.inputPtr = 65536;
        this.outputPtr = 65536 + 128 * 4;

        // Pending note events before WASM is ready
        this.pendingNotes = [];
        this.pendingParams = [];

        // Sample rate from AudioContext
        this.sampleRateValue = options.processorOptions?.sampleRate || sampleRate;

        // Application info
        this.appName = 'Unknown';
        this.appVendor = 'Unknown';
        this.appVersion = '0.0.0';

        // Message handler
        this.port.onmessage = (e) => this._handleMessage(e.data);
    }

    async _initWasm(wasmBytes) {
        try {
            // Try to detect WASM import requirements by attempting instantiation
            // Different build configurations produce different import requirements:
            // 1. WASI-style: wasi_snapshot_preview1 + env modules
            // 2. Emscripten minified: 'a' module with minified function names

            // WASI-style import object
            const wasiImportObject = {
                wasi_snapshot_preview1: {
                    args_sizes_get: () => 0,
                    args_get: () => 0,
                    proc_exit: () => {}
                },
                env: {
                    __main_argc_argv: () => 0
                }
            };

            // Emscripten minified import object (for -O3 builds)
            const emscriptenImportObject = {
                a: {
                    // Common Emscripten imports (minified names)
                    a: () => 0,  // _emscripten_resize_heap
                    b: () => 0,  // _emscripten_asm_const_int
                    c: () => 0,  // other stubs
                    d: () => 0,
                    e: () => 0
                }
            };

            // Try WASI-style first, then Emscripten minified
            let result;
            try {
                result = await WebAssembly.instantiate(wasmBytes, wasiImportObject);
                console.log('[UmimGeneric] WASM instantiated with WASI imports');
            } catch (wasiErr) {
                console.log('[UmimGeneric] WASI imports failed, trying Emscripten minified:', wasiErr.message);
                try {
                    result = await WebAssembly.instantiate(wasmBytes, emscriptenImportObject);
                    console.log('[UmimGeneric] WASM instantiated with Emscripten minified imports');
                } catch (emErr) {
                    // If both fail, throw the original error with more context
                    throw new Error(`WASM instantiation failed. WASI: ${wasiErr.message}, Emscripten: ${emErr.message}`);
                }
            }
            const exports = result.instance.exports;

            // Debug: log all exports
            console.log('[UmimGeneric] WASM exports:', Object.keys(exports).sort().join(', '));

            // Map exports - handle both direct names and minified names
            // The WASM may export functions directly (umi_process) or via Emscripten wrappers
            this.wasmExports = exports;
            this.wasmMemory = exports.memory;

            // Create function mappings - try both direct and underscore-prefixed names
            this.fn = {
                create: exports.umi_create || exports._umi_create,
                destroy: exports.umi_destroy || exports._umi_destroy,
                process: exports.umi_process || exports._umi_process,
                noteOn: exports.umi_note_on || exports._umi_note_on,
                noteOff: exports.umi_note_off || exports._umi_note_off,
                setParam: exports.umi_set_param || exports._umi_set_param,
                getParam: exports.umi_get_param || exports._umi_get_param,
                getParamCount: exports.umi_get_param_count || exports._umi_get_param_count,
                getParamName: exports.umi_get_param_name || exports._umi_get_param_name,
                getParamMin: exports.umi_get_param_min || exports._umi_get_param_min,
                getParamMax: exports.umi_get_param_max || exports._umi_get_param_max,
                getParamDefault: exports.umi_get_param_default || exports._umi_get_param_default,
                getName: exports.umi_get_name || exports._umi_get_name,
                getVendor: exports.umi_get_vendor || exports._umi_get_vendor,
                getVersion: exports.umi_get_version || exports._umi_get_version,
                processCC: exports.umi_process_cc || exports._umi_process_cc,
            };

            console.log('[UmimGeneric] Function mappings:',
                'create=', !!this.fn.create,
                'process=', !!this.fn.process,
                'noteOn=', !!this.fn.noteOn);

            // Initialize Emscripten runtime - call __wasm_call_ctors if available
            // Do NOT call _start - it causes unreachable trap
            if (exports.__wasm_call_ctors) {
                exports.__wasm_call_ctors();
            }

            // Initialize synth with umi_create
            if (this.fn.create) {
                this.fn.create();
            }

            // Get application info
            this.appName = this._readCString(this.fn.getName?.()) || 'Unknown';
            this.appVendor = this._readCString(this.fn.getVendor?.()) || 'Unknown';
            this.appVersion = this._readCString(this.fn.getVersion?.()) || '0.0.0';

            // Process pending notes
            for (const note of this.pendingNotes) {
                if (note.on) {
                    this.fn.noteOn?.(note.note, note.velocity);
                } else {
                    this.fn.noteOff?.(note.note);
                }
            }
            this.pendingNotes = [];

            // Process pending params
            for (const param of this.pendingParams) {
                this.fn.setParam?.(param.index, param.value);
            }
            this.pendingParams = [];

            this.ready = true;

            // Collect parameter info
            const params = this._getParameterInfo();

            this.port.postMessage({
                type: 'ready',
                name: this.appName,
                vendor: this.appVendor,
                version: this.appVersion,
                params: params
            });

            console.log(`[UmimGeneric] WASM initialized: ${this.appName} v${this.appVersion} by ${this.appVendor}`);

        } catch (err) {
            console.error('[UmimGeneric] WASM init error:', err);
            this.port.postMessage({ type: 'error', message: err.message });
        }
    }

    _readCString(ptr) {
        if (!ptr || !this.wasmMemory) return '';
        const heap = new Uint8Array(this.wasmMemory.buffer);
        let end = ptr;
        while (heap[end] !== 0 && end < heap.length) end++;
        let str = '';
        for (let i = ptr; i < end; i++) {
            str += String.fromCharCode(heap[i]);
        }
        return str;
    }

    _getParameterInfo() {
        if (!this.fn) return [];

        const count = this.fn.getParamCount?.() || 0;
        const params = [];

        for (let i = 0; i < count; i++) {
            params.push({
                index: i,
                name: this._readCString(this.fn.getParamName?.(i)) || `Param ${i}`,
                min: this.fn.getParamMin?.(i) ?? 0,
                max: this.fn.getParamMax?.(i) ?? 1,
                default: this.fn.getParamDefault?.(i) ?? 0,
                value: this.fn.getParam?.(i) ?? 0
            });
        }

        return params;
    }

    _handleMessage(msg) {
        switch (msg.type) {
            case 'init':
                // Receive WASM bytes and initialize
                if (msg.wasmBytes) {
                    this._initWasm(msg.wasmBytes);
                }
                break;

            case 'init-embedded':
                // Initialize with embedded WASM (fallback mode)
                try {
                    const wasmBytes = base64ToArrayBuffer(EMBEDDED_WASM_BASE64);
                    this._initWasm(wasmBytes);
                } catch (err) {
                    console.error('[UmimGeneric] Failed to decode embedded WASM:', err);
                    this.port.postMessage({ type: 'error', message: err.message });
                }
                break;

            case 'note-on':
                if (this.ready && this.fn) {
                    this.fn.noteOn?.(msg.note, msg.velocity);
                } else {
                    this.pendingNotes.push({ on: true, note: msg.note, velocity: msg.velocity });
                }
                break;

            case 'note-off':
                if (this.ready && this.fn) {
                    this.fn.noteOff?.(msg.note);
                } else {
                    this.pendingNotes.push({ on: false, note: msg.note });
                }
                break;

            case 'set-param':
                if (this.ready && this.fn) {
                    this.fn.setParam?.(msg.index, msg.value);
                } else {
                    this.pendingParams.push({ index: msg.index, value: msg.value });
                }
                break;

            case 'get-params':
                if (this.ready) {
                    this.port.postMessage({
                        type: 'params',
                        params: this._getParameterInfo()
                    });
                }
                break;

            case 'midi':
                // Generic MIDI message handling
                if (this.ready && this.fn?.processCC && msg.data) {
                    const status = msg.data[0];
                    const cmd = status & 0xF0;
                    if (cmd === 0xB0 && msg.data.length >= 3) {
                        // CC
                        this.fn.processCC(msg.data[1], msg.data[2]);
                    }
                }
                break;
        }
    }

    process(inputs, outputs, parameters) {
        const output = outputs[0];
        if (!output || output.length === 0) return true;

        const outputChannel = output[0];
        const numFrames = outputChannel.length;

        if (!this.ready || !this.fn?.process) {
            outputChannel.fill(0);
            return true;
        }

        try {
            // Call umi_process with 4 arguments: input, output, frames, sample_rate
            this.fn.process(
                this.inputPtr,
                this.outputPtr,
                numFrames,
                Math.round(this.sampleRateValue)
            );

            // Copy output from WASM memory
            const heap = new Float32Array(this.wasmMemory.buffer);
            const start = this.outputPtr / 4;
            outputChannel.set(heap.subarray(start, start + numFrames));

            // Copy to all channels
            for (let ch = 1; ch < output.length; ch++) {
                output[ch].set(outputChannel);
            }

        } catch (err) {
            console.error('[UmimGeneric] process error:', err);
            outputChannel.fill(0);
        }

        return true;
    }
}

// Register processor
try {
    registerProcessor('umim-generic-processor', UmimGenericProcessor);
    console.log('[UmimGeneric] Processor registered');
} catch (err) {
    console.error('[UmimGeneric] Failed to register processor:', err);
}
