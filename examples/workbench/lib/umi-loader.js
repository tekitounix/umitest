// =====================================================================
// UMI-OS UMIM Loader
// =====================================================================
// Simple loader for .umim files (WASM AudioWorklet modules)
// Usage:
//   const node = await UmiLoader.load(audioContext, './synth.umim', 'umi-synth-processor');
// =====================================================================

class UmiLoader {
  /**
   * Load a UMIM module and create an AudioWorkletNode
   * @param {AudioContext} audioContext - The audio context
   * @param {string} umimPath - Path to the .umim file
   * @param {string} processorName - Name of the processor (defined in preamble)
   * @param {Object} options - Optional AudioWorkletNode options
   * @returns {Promise<AudioWorkletNode>} The created AudioWorkletNode
   */
  static async load(audioContext, umimPath, processorName, options = {}) {
    // Load the UMIM module (addModule handles .umim as JS)
    await audioContext.audioWorklet.addModule(umimPath);
    
    // Create the AudioWorkletNode
    const node = new AudioWorkletNode(audioContext, processorName, {
      processorOptions: {
        sampleRate: audioContext.sampleRate,
        ...options.processorOptions
      },
      ...options
    });
    
    // Wrap with convenience methods
    return new UmiNode(node);
  }
}

/**
 * Wrapper for AudioWorkletNode with UMIM-specific methods
 */
class UmiNode {
  constructor(node) {
    this.node = node;
    this.ready = false;
    
    // Forward ready event
    this.readyPromise = new Promise((resolve) => {
      node.port.onmessage = (e) => {
        if (e.data.type === 'ready') {
          this.ready = true;
          resolve();
        }
      };
    });
  }
  
  // Wait for WASM to be ready
  async waitReady() {
    return this.readyPromise;
  }
  
  // Connect to destination
  connect(destination) {
    this.node.connect(destination);
    return this;
  }
  
  // Disconnect
  disconnect() {
    this.node.disconnect();
    return this;
  }
  
  // MIDI note on
  noteOn(note, velocity = 127) {
    this.node.port.postMessage({ type: 'note-on', note, velocity });
    return this;
  }
  
  // MIDI note off
  noteOff(note) {
    this.node.port.postMessage({ type: 'note-off', note });
    return this;
  }
  
  // Set parameter
  setParam(index, value) {
    this.node.port.postMessage({ type: 'set-param', index, value });
    return this;
  }
  
  // Get underlying AudioWorkletNode
  getNode() {
    return this.node;
  }
}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
  module.exports = { UmiLoader, UmiNode };
}
