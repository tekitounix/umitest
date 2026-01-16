/**
 * UMI-OS AudioWorklet Processor
 * 
 * Runs on separate audio thread to prevent UI blocking.
 */

class UmiProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.wasmModule = null;
    this.outputPtr = null;
    this.isReady = false;
    this.pendingParams = new Map();  // Buffer for parameter updates
    
    // Handle messages from main thread
    this.port.onmessage = (event) => {
      const { type, data } = event.data;
      
      switch (type) {
        case 'init':
          this.initWasm(data);
          break;
        case 'noteOn':
          if (this.isReady) {
            this.wasmModule._umi_note_on(null, data.note, data.velocity);
          }
          break;
        case 'noteOff':
          if (this.isReady) {
            this.wasmModule._umi_note_off(null, data.note);
          }
          break;
        case 'setParam':
          if (this.isReady) {
            this.wasmModule._umi_set_param(null, data.id, data.value);
          }
          break;
      }
    };
  }
  
  async initWasm(data) {
    try {
      // Import WASM module in worklet context
      const { wasmBinary, sampleRate } = data;
      
      // Create WASM instance from binary
      const wasmModule = await WebAssembly.instantiate(wasmBinary, {
        env: {
          memory: new WebAssembly.Memory({ initial: 256, maximum: 256 }),
        },
        wasi_snapshot_preview1: {
          fd_write: () => 0,
          fd_seek: () => 0,
          fd_close: () => 0,
          proc_exit: () => {},
        }
      });
      
      this.wasmInstance = wasmModule.instance;
      this.wasmExports = wasmModule.instance.exports;
      
      // Initialize synth
      if (this.wasmExports._umi_create) {
        this.wasmExports._umi_create(sampleRate);
      } else if (this.wasmExports.umi_create) {
        this.wasmExports.umi_create(sampleRate);
      }
      
      // Allocate output buffer (128 samples * 4 bytes per float)
      const bufferSize = 128;
      if (this.wasmExports._malloc) {
        this.outputPtr = this.wasmExports._malloc(bufferSize * 4);
      } else if (this.wasmExports.malloc) {
        this.outputPtr = this.wasmExports.malloc(bufferSize * 4);
      }
      
      // Get memory
      this.memory = this.wasmExports.memory;
      
      this.isReady = true;
      this.port.postMessage({ type: 'ready' });
      
    } catch (e) {
      console.error('Worklet WASM init error:', e);
      this.port.postMessage({ type: 'error', error: e.message });
    }
  }
  
  process(inputs, outputs, parameters) {
    if (!this.isReady || !this.outputPtr) {
      return true;
    }
    
    const output = outputs[0];
    if (!output || output.length === 0) {
      return true;
    }
    
    const channel = output[0];
    const bufferSize = channel.length;  // Usually 128
    
    // Process audio
    const processFunc = this.wasmExports._umi_process || this.wasmExports.umi_process;
    if (processFunc) {
      processFunc(null, this.outputPtr, bufferSize);
    }
    
    // Copy from WASM memory to output
    const floatView = new Float32Array(this.memory.buffer, this.outputPtr, bufferSize);
    channel.set(floatView);
    
    return true;
  }
}

registerProcessor('umi-processor', UmiProcessor);
