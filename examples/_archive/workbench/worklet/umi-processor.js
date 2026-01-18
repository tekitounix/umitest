// =====================================================================
// UMI-OS Generic AudioWorklet Processor
// =====================================================================
// This file is appended to Emscripten output via --post-js
// Processor name is configured via UMI_PROCESSOR_NAME global variable
// =====================================================================

// HeapAudioBuffer: Helper class for managing audio data in WASM heap
class HeapAudioBuffer {
  constructor(wasmModule, length, numChannels = 1) {
    this.module = wasmModule;
    this.length = length;
    this.channelData = [];
    
    for (let i = 0; i < numChannels; i++) {
      const channelPtr = wasmModule._malloc(length * 4);
      this.channelData.push(channelPtr);
    }
  }
  
  getChannelData(channel) {
    const ptr = this.channelData[channel];
    return new Float32Array(this.module.HEAPF32.buffer, ptr, this.length);
  }
  
  free() {
    for (const ptr of this.channelData) {
      this.module._free(ptr);
    }
  }
}

// Generic UMIM Processor
class UmiProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    
    this.ready = false;
    this.inputHeapBuffer = null;
    this.outputHeapBuffer = null;
    this.pendingNotes = [];
    this.pendingParams = new Map();
    
    this._initWasm(options.processorOptions || {});
    this.port.onmessage = (e) => this._handleMessage(e.data);
  }
  
  _initWasm(opts) {
    const sampleRateToUse = opts.sampleRate || sampleRate;
    
    try {
      if (typeof Module !== 'undefined' && Module._umi_create) {
        Module._umi_create(sampleRateToUse);
        
        this.inputHeapBuffer = new HeapAudioBuffer(Module, 128, 1);
        this.outputHeapBuffer = new HeapAudioBuffer(Module, 128, 1);
        
        // Apply pending operations
        for (const note of this.pendingNotes) {
          if (note.on) {
            Module._umi_note_on(note.note, note.velocity);
          } else {
            Module._umi_note_off(note.note);
          }
        }
        this.pendingNotes = [];
        
        for (const [index, value] of this.pendingParams) {
          Module._umi_set_param(index, value);
        }
        this.pendingParams.clear();
        
        this.ready = true;
        this.port.postMessage({ type: 'ready' });
      } else {
        console.error('UmiProcessor: Module not available');
        this.port.postMessage({ type: 'error', message: 'Module not available' });
      }
    } catch (err) {
      console.error('UmiProcessor init error:', err);
      this.port.postMessage({ type: 'error', message: err.message });
    }
  }
  
  _handleMessage(msg) {
    switch (msg.type) {
      case 'note-on':
        if (this.ready) {
          Module._umi_note_on(msg.note, msg.velocity);
        } else {
          this.pendingNotes.push({ on: true, note: msg.note, velocity: msg.velocity });
        }
        break;
        
      case 'note-off':
        if (this.ready) {
          Module._umi_note_off(msg.note);
        } else {
          this.pendingNotes.push({ on: false, note: msg.note });
        }
        break;
        
      case 'set-param':
        if (this.ready) {
          Module._umi_set_param(msg.index, msg.value);
        } else {
          this.pendingParams.set(msg.index, msg.value);
        }
        break;
        
      case 'cc':
        if (this.ready && Module._umi_process_cc) {
          Module._umi_process_cc(msg.channel || 0, msg.controller, msg.value);
        }
        break;
    }
  }
  
  process(inputs, outputs, parameters) {
    if (!this.ready) return true;
    
    const input = inputs[0];
    const output = outputs[0];
    
    if (!output || output.length === 0) return true;
    
    const inputChannel = input && input.length > 0 ? input[0] : null;
    const outputChannel = output[0];
    const numFrames = outputChannel.length;
    
    try {
      const inputView = this.inputHeapBuffer.getChannelData(0);
      
      if (inputChannel && inputChannel.length === numFrames) {
        inputView.set(inputChannel);
      } else {
        inputView.fill(0);
      }
      
      Module._umi_process(
        this.inputHeapBuffer.channelData[0],
        this.outputHeapBuffer.channelData[0],
        numFrames
      );
      
      const outputView = this.outputHeapBuffer.getChannelData(0);
      outputChannel.set(outputView);
      
      // Copy to other channels (mono to stereo)
      for (let ch = 1; ch < output.length; ch++) {
        output[ch].set(outputView);
      }
    } catch (err) {
      outputChannel.fill(0);
    }
    
    return true;
  }
}

// Register the processor with name from WASM
function registerUmiProcessor() {
  // Get processor name from WASM module
  let processorName = 'umi-processor';
  
  if (typeof Module !== 'undefined' && Module._umi_get_processor_name) {
    const namePtr = Module._umi_get_processor_name();
    if (namePtr) {
      processorName = Module.UTF8ToString(namePtr);
    }
  }
  
  try {
    registerProcessor(processorName, UmiProcessor);
    console.log(`${processorName} registered successfully`);
  } catch (err) {
    console.error(`Failed to register ${processorName}:`, err);
  }
}

// Wait for WASM initialization with polling fallback
function waitAndRegister() {
  console.log('[UMI] Checking Module state...');

  if (typeof Module === 'undefined') {
    console.error('[UMI] Module not defined in AudioWorkletGlobalScope');
    return;
  }

  // Check if already initialized
  if (Module.calledRun) {
    console.log('[UMI] Module already initialized');
    registerUmiProcessor();
    return;
  }

  // Set callback
  const existingCallback = Module.onRuntimeInitialized;
  Module.onRuntimeInitialized = function() {
    console.log('[UMI] onRuntimeInitialized called');
    if (existingCallback) existingCallback();
    registerUmiProcessor();
  };

  // Polling fallback (some versions of Emscripten may not call onRuntimeInitialized)
  let attempts = 0;
  const pollInterval = setInterval(() => {
    attempts++;
    if (Module.calledRun || (Module._umi_create && Module._umi_process)) {
      console.log(`[UMI] Module ready after ${attempts} polls`);
      clearInterval(pollInterval);
      if (!Module._registered) {
        Module._registered = true;
        registerUmiProcessor();
      }
    } else if (attempts > 100) {
      console.error('[UMI] Timeout waiting for Module initialization');
      clearInterval(pollInterval);
    }
  }, 10);
}

waitAndRegister();
