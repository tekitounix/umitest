/**
 * UMI-OS Web Interface
 * 
 * Main JavaScript module for browser integration.
 * Handles AudioContext, MIDI, and WASM initialization.
 */

class UmiSynth {
  constructor() {
    this.audioContext = null;
    this.workletNode = null;
    this.wasmModule = null;
    this.isReady = false;
    this.midiAccess = null;
  }
  
  /**
   * Initialize the synthesizer
   * @param {Object} options - Configuration options
   * @param {number} options.sampleRate - Sample rate (default: 48000)
   * @returns {Promise<void>}
   */
  async init(options = {}) {
    const sampleRate = options.sampleRate || 48000;
    
    // Create AudioContext
    this.audioContext = new AudioContext({ sampleRate });
    
    // Load WASM module
    const wasmUrl = options.wasmUrl || '../.build/wasm/umi_synth.js';
    try {
      const module = await import(wasmUrl);
      this.wasmModule = await module.default();
      console.log('WASM module loaded');
    } catch (e) {
      console.error('Failed to load WASM:', e);
      throw e;
    }
    
    // Initialize WASM synth
    this.wasmModule._umi_create(sampleRate);
    
    // Create ScriptProcessor fallback (simpler than AudioWorklet for demo)
    this.setupScriptProcessor();
    
    this.isReady = true;
    console.log('UMI Synth ready');
  }
  
  /**
   * Setup ScriptProcessor for audio output with ring buffer
   * Ring buffer provides lookahead to prevent audio dropouts from UI blocking
   */
  setupScriptProcessor() {
    const bufferSize = 512;  // Larger buffer for more stability
    const processor = this.audioContext.createScriptProcessor(bufferSize, 0, 1);
    const wasmModule = this.wasmModule;
    
    // Allocate a buffer in WASM memory for audio output
    const outputPtr = wasmModule._malloc(bufferSize * 4);  // 4 bytes per float
    
    // Ring buffer for lookahead (8 buffers = ~85ms at 48kHz with 512 samples)
    const ringBufferCount = 8;
    const ringBuffer = new Array(ringBufferCount);
    for (let i = 0; i < ringBufferCount; i++) {
      ringBuffer[i] = new Float32Array(bufferSize);
    }
    let writeIndex = 0;
    let readIndex = 0;
    let bufferedCount = 0;
    let isFillingBuffer = false;
    
    // Get memory view
    let getMemoryView = null;
    
    if (wasmModule.HEAPF32) {
      getMemoryView = () => wasmModule.HEAPF32;
    } else if (wasmModule.wasmMemory) {
      getMemoryView = () => new Float32Array(wasmModule.wasmMemory.buffer);
    } else {
      for (const key of Object.keys(wasmModule)) {
        const val = wasmModule[key];
        if (val && val.buffer instanceof ArrayBuffer) {
          getMemoryView = () => new Float32Array(wasmModule[key].buffer);
          break;
        }
      }
    }
    
    if (!getMemoryView) {
      console.error('Could not find WASM memory!');
    }
    
    // Generate one buffer
    const generateBuffer = () => {
      if (bufferedCount >= ringBufferCount) return false;
      
      wasmModule._umi_process(null, outputPtr, bufferSize);
      
      if (getMemoryView) {
        const memory = getMemoryView();
        const floatOffset = outputPtr >> 2;
        const dest = ringBuffer[writeIndex];
        for (let i = 0; i < bufferSize; i++) {
          dest[i] = memory[floatOffset + i];
        }
      }
      
      writeIndex = (writeIndex + 1) % ringBufferCount;
      bufferedCount++;
      return true;
    };
    
    // Fill buffer incrementally using requestAnimationFrame for smoother scheduling
    const scheduleFill = () => {
      if (isFillingBuffer) return;
      isFillingBuffer = true;
      
      const fillStep = () => {
        // Generate 2 buffers per frame to keep up
        generateBuffer();
        generateBuffer();
        
        if (bufferedCount < ringBufferCount) {
          requestAnimationFrame(fillStep);
        } else {
          isFillingBuffer = false;
        }
      };
      
      requestAnimationFrame(fillStep);
    };
    
    // Initial fill (synchronous for first audio)
    while (bufferedCount < ringBufferCount) {
      generateBuffer();
    }
    
    processor.onaudioprocess = (event) => {
      const output = event.outputBuffer.getChannelData(0);
      
      if (bufferedCount > 0) {
        // Read from ring buffer
        output.set(ringBuffer[readIndex]);
        readIndex = (readIndex + 1) % ringBufferCount;
        bufferedCount--;
        
        // Schedule refill when buffer is half empty
        if (bufferedCount <= ringBufferCount / 2) {
          scheduleFill();
        }
      } else {
        // Buffer underrun - generate directly (last resort)
        console.warn('Audio buffer underrun!');
        wasmModule._umi_process(null, outputPtr, bufferSize);
        if (getMemoryView) {
          const memory = getMemoryView();
          const floatOffset = outputPtr >> 2;
          for (let i = 0; i < bufferSize; i++) {
            output[i] = memory[floatOffset + i];
          }
        }
        scheduleFill();
      }
    };
    
    processor.connect(this.audioContext.destination);
    this.processorNode = processor;
    this.outputPtr = outputPtr;
  }
  
  /**
   * Start audio (required after user gesture)
   */
  async start() {
    if (this.audioContext.state === 'suspended') {
      await this.audioContext.resume();
    }
  }
  
  /**
   * Stop audio
   */
  async stop() {
    if (this.audioContext.state === 'running') {
      await this.audioContext.suspend();
    }
  }
  
  /**
   * Send note on
   * @param {number} note - MIDI note number (0-127)
   * @param {number} velocity - Velocity (0-127)
   */
  noteOn(note, velocity = 100) {
    if (!this.isReady) return;
    this.wasmModule._umi_note_on(null, note, velocity);
  }
  
  /**
   * Send note off
   * @param {number} note - MIDI note number (0-127)
   */
  noteOff(note) {
    if (!this.isReady) return;
    this.wasmModule._umi_note_off(null, note);
  }
  
  /**
   * Set parameter
   * @param {number} id - Parameter ID
   * @param {number} value - Parameter value
   */
  setParam(id, value) {
    if (!this.isReady) return;
    this.wasmModule._umi_set_param(null, id, value);
  }
  
  /**
   * Set master volume (0-1)
   */
  setVolume(value) {
    this.setParam(0, value);
  }
  
  /**
   * Set filter cutoff (20-20000 Hz)
   */
  setFilterCutoff(value) {
    this.setParam(1, value);
  }
  
  /**
   * Set filter resonance (0-1)
   */
  setFilterResonance(value) {
    this.setParam(2, value);
  }
  
  /**
   * Initialize Web MIDI
   * @returns {Promise<void>}
   */
  async initMidi() {
    if (!navigator.requestMIDIAccess) {
      console.warn('Web MIDI not supported');
      return;
    }
    
    try {
      this.midiAccess = await navigator.requestMIDIAccess();
      
      // Connect all MIDI inputs
      for (const input of this.midiAccess.inputs.values()) {
        this.connectMidiInput(input);
      }
      
      // Listen for new devices
      this.midiAccess.onstatechange = (event) => {
        if (event.port.type === 'input' && event.port.state === 'connected') {
          this.connectMidiInput(event.port);
        }
      };
      
      console.log('MIDI initialized');
    } catch (e) {
      console.error('MIDI initialization failed:', e);
    }
  }
  
  /**
   * Connect a MIDI input
   */
  connectMidiInput(input) {
    console.log(`MIDI input connected: ${input.name}`);
    
    input.onmidimessage = (event) => {
      const [status, data1, data2] = event.data;
      const command = status & 0xf0;
      
      switch (command) {
        case 0x90: // Note On
          if (data2 > 0) {
            this.noteOn(data1, data2);
          } else {
            this.noteOff(data1);
          }
          break;
        case 0x80: // Note Off
          this.noteOff(data1);
          break;
        case 0xb0: // Control Change
          this.handleCC(data1, data2);
          break;
      }
    };
  }
  
  /**
   * Handle MIDI CC
   */
  handleCC(cc, value) {
    const normalized = value / 127;
    
    switch (cc) {
      case 1:  // Mod wheel -> filter cutoff
        this.setFilterCutoff(20 + normalized * 19980);
        break;
      case 7:  // Volume
        this.setVolume(normalized);
        break;
      case 74: // Cutoff (standard)
        this.setFilterCutoff(20 + normalized * 19980);
        break;
      case 71: // Resonance (standard)
        this.setFilterResonance(normalized);
        break;
    }
  }
  
  /**
   * Cleanup
   */
  destroy() {
    if (this.processorNode) {
      this.processorNode.disconnect();
    }
    if (this.audioContext) {
      this.audioContext.close();
    }
  }
  
  // =========================================================================
  // Metadata API - for automatic UI generation
  // =========================================================================
  
  /**
   * Get all parameter descriptors from WASM
   * @returns {Array<{id: number, name: string, defaultValue: number, min: number, max: number}>}
   */
  getParamDescriptors() {
    if (!this.wasmModule) return [];
    
    const count = this.wasmModule._umi_get_param_count(null);
    const params = [];
    
    for (let i = 0; i < count; i++) {
      // Get name
      const namePtr = this.wasmModule._umi_get_param_name(null, i);
      const name = this.wasmModule.UTF8ToString(namePtr);
      
      // Get info: [id, default, min, max]
      const infoPtr = this.wasmModule._umi_get_param_info(null, i);
      const info = new Float32Array(this.wasmModule.HEAPF32.buffer, infoPtr, 4);
      
      params.push({
        id: Math.round(info[0]),
        name: name,
        defaultValue: info[1],
        min: info[2],
        max: info[3]
      });
    }
    
    return params;
  }
  
  /**
   * Generate UI controls from parameter descriptors
   * @param {HTMLElement} container - Container element for generated UI
   * @param {Object} options - UI options
   * @returns {Object} - Control references for testing
   */
  generateUI(container, options = {}) {
    const params = this.getParamDescriptors();
    const controls = {};
    
    // Clear container
    container.innerHTML = '';
    
    // CSS for generated controls
    const style = document.createElement('style');
    style.textContent = `
      .umi-control { margin: 10px 0; display: flex; align-items: center; gap: 10px; }
      .umi-control label { min-width: 120px; }
      .umi-control input[type="range"] { flex: 1; }
      .umi-control .value { min-width: 80px; text-align: right; font-family: monospace; }
    `;
    container.appendChild(style);
    
    // Generate control for each parameter
    for (const param of params) {
      const div = document.createElement('div');
      div.className = 'umi-control';
      div.dataset.paramId = param.id;
      
      const label = document.createElement('label');
      label.textContent = param.name;
      
      const input = document.createElement('input');
      input.type = 'range';
      input.min = param.min;
      input.max = param.max;
      input.step = (param.max - param.min) / 100;  // Coarser step
      input.value = param.defaultValue;
      input.dataset.paramId = param.id;
      
      const valueDisplay = document.createElement('span');
      valueDisplay.className = 'value';
      valueDisplay.textContent = this.formatValue(param, param.defaultValue);
      
      // Throttled event handler - update at most every 16ms (60fps)
      let lastUpdate = 0;
      let pendingValue = null;
      const synth = this;
      
      const updateParam = () => {
        if (pendingValue !== null) {
          synth.setParam(param.id, pendingValue);
          valueDisplay.textContent = synth.formatValue(param, pendingValue);
          container.dispatchEvent(new CustomEvent('paramchange', {
            detail: { id: param.id, value: pendingValue, name: param.name }
          }));
          pendingValue = null;
        }
      };
      
      input.addEventListener('input', () => {
        const value = parseFloat(input.value);
        const now = performance.now();
        
        if (now - lastUpdate > 16) {
          // Immediate update
          synth.setParam(param.id, value);
          valueDisplay.textContent = synth.formatValue(param, value);
          lastUpdate = now;
        } else {
          // Schedule update
          pendingValue = value;
          setTimeout(updateParam, 16 - (now - lastUpdate));
        }
      });
      
      // Update on mouse/touch release
      input.addEventListener('change', () => {
        const value = parseFloat(input.value);
        synth.setParam(param.id, value);
        valueDisplay.textContent = synth.formatValue(param, value);
      });
      
      div.appendChild(label);
      div.appendChild(input);
      div.appendChild(valueDisplay);
      container.appendChild(div);
      
      controls[param.id] = { input, valueDisplay, param };
    }
    
    return controls;
  }
  
  /**
   * Format parameter value for display
   */
  formatValue(param, value) {
    if (param.max > 1000) {
      return `${Math.round(value)} Hz`;
    } else if (param.max <= 1) {
      return value.toFixed(2);
    } else {
      return value.toFixed(1);
    }
  }
}

// Export for ES modules and global scope
export { UmiSynth };

// Make available globally for non-module scripts
if (typeof window !== 'undefined') {
  window.UmiSynth = UmiSynth;
}
