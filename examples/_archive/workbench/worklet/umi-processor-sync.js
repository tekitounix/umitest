// =====================================================================
// UMI-OS AudioWorklet Processor (Synchronous Registration)
// =====================================================================
// Register processor immediately, wait for WASM inside process()
// =====================================================================

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

class UmiSynthProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();

    this.ready = false;
    this.wasmInitAttempts = 0;
    this.inputHeapBuffer = null;
    this.outputHeapBuffer = null;
    this.pendingNotes = [];
    this.pendingParams = new Map();
    this.sampleRateToUse = options.processorOptions?.sampleRate || sampleRate;

    this.port.onmessage = (e) => this._handleMessage(e.data);

    console.log('[UmiSynthProcessor] Created, will init WASM in process()');
  }

  _tryInitWasm() {
    this.wasmInitAttempts++;

    if (typeof Module === 'undefined') {
      return false;
    }

    if (!Module._umi_create || !Module._umi_process) {
      return false;
    }

    try {
      Module._umi_create(this.sampleRateToUse);

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
      console.log(`[UmiSynthProcessor] WASM initialized after ${this.wasmInitAttempts} attempts`);
      return true;

    } catch (err) {
      console.error('[UmiSynthProcessor] Init error:', err);
      return false;
    }
  }

  _handleMessage(msg) {
    console.log('[UmiSynthProcessor] Message:', msg.type, msg);
    switch (msg.type) {
      case 'note-on':
        console.log('[UmiSynthProcessor] Note ON:', msg.note, msg.velocity);
        if (this.ready) {
          Module._umi_note_on(msg.note, msg.velocity);
        } else {
          this.pendingNotes.push({ on: true, note: msg.note, velocity: msg.velocity });
        }
        break;

      case 'note-off':
        console.log('[UmiSynthProcessor] Note OFF:', msg.note);
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
    }
  }

  process(inputs, outputs, parameters) {
    // Try to init WASM if not ready
    if (!this.ready) {
      if (this.wasmInitAttempts < 1000) {
        this._tryInitWasm();
      }
      // Output silence while not ready
      if (outputs[0] && outputs[0][0]) {
        outputs[0][0].fill(0);
      }
      return true;
    }

    const output = outputs[0];
    if (!output || output.length === 0) return true;

    const outputChannel = output[0];
    const numFrames = outputChannel.length;

    try {
      const inputView = this.inputHeapBuffer.getChannelData(0);
      inputView.fill(0); // Synth doesn't use input

      Module._umi_process(
        this.inputHeapBuffer.channelData[0],
        this.outputHeapBuffer.channelData[0],
        numFrames
      );

      const outputView = this.outputHeapBuffer.getChannelData(0);
      outputChannel.set(outputView);

      // Copy to other channels
      for (let ch = 1; ch < output.length; ch++) {
        output[ch].set(outputView);
      }
    } catch (err) {
      outputChannel.fill(0);
    }

    return true;
  }
}

// Register immediately with known name
try {
  registerProcessor('umi-synth-processor', UmiSynthProcessor);
  console.log('[UMI] umi-synth-processor registered');
} catch (err) {
  console.error('[UMI] Failed to register:', err);
}
