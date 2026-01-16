/**
 * UMI-OS AudioWorklet Processor
 * 
 * This file runs in the AudioWorklet scope.
 * It receives audio processing requests and communicates with WASM.
 */

class UmiProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    
    this.synth = null;
    this.wasmReady = false;
    this.sampleRate = options.processorOptions?.sampleRate || 48000;
    
    // Handle messages from main thread
    this.port.onmessage = (event) => {
      const { type, data } = event.data;
      
      switch (type) {
        case 'init':
          this.initWasm(data.wasmModule, data.wasmMemory);
          break;
        case 'noteOn':
          if (this.wasmReady) {
            this.noteOn(data.note, data.velocity);
          }
          break;
        case 'noteOff':
          if (this.wasmReady) {
            this.noteOff(data.note);
          }
          break;
        case 'setParam':
          if (this.wasmReady) {
            this.setParam(data.id, data.value);
          }
          break;
      }
    };
  }
  
  async initWasm(wasmModule, wasmMemory) {
    try {
      // WASM module should be pre-instantiated and passed via message
      // For now, we'll use cwrap functions set on globalThis
      if (globalThis._umi_create) {
        this.synth = globalThis._umi_create(this.sampleRate);
        this.wasmReady = true;
        this.port.postMessage({ type: 'ready' });
      }
    } catch (e) {
      this.port.postMessage({ type: 'error', message: e.toString() });
    }
  }
  
  noteOn(note, velocity) {
    if (globalThis._umi_note_on) {
      globalThis._umi_note_on(this.synth, note, velocity);
    }
  }
  
  noteOff(note) {
    if (globalThis._umi_note_off) {
      globalThis._umi_note_off(this.synth, note);
    }
  }
  
  setParam(id, value) {
    if (globalThis._umi_set_param) {
      globalThis._umi_set_param(this.synth, id, value);
    }
  }
  
  process(inputs, outputs, parameters) {
    if (!this.wasmReady) {
      return true;
    }
    
    const output = outputs[0];
    if (!output || !output[0]) {
      return true;
    }
    
    const frames = output[0].length;
    
    // Get buffer pointer from WASM
    if (globalThis._umi_process && globalThis._umi_get_buffer_ptr) {
      const bufferPtr = globalThis._umi_get_buffer_ptr(this.synth);
      globalThis._umi_process(this.synth, bufferPtr, frames);
      
      // Copy WASM buffer to output
      const wasmBuffer = new Float32Array(
        globalThis.wasmMemory.buffer,
        bufferPtr,
        frames
      );
      
      // Copy to all output channels (mono to stereo)
      for (let channel = 0; channel < output.length; channel++) {
        output[channel].set(wasmBuffer);
      }
    }
    
    return true;
  }
}

registerProcessor('umi-processor', UmiProcessor);
