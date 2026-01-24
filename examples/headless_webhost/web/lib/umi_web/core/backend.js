/**
 * UMI Web - Backend Interface
 *
 * Abstract base class for all audio backends.
 * Provides unified API for MIDI, audio, and parameter control.
 *
 * @module umi_web/core/backend
 */

/**
 * Backend types enumeration
 */
export const BackendType = {
    UMIM: 'umim',           // DSP only (lightweight)
    UMIOS: 'umios',         // Full UMI-OS kernel simulation
    RENODE: 'renode',       // Renode hardware simulation
    CORTEXM: 'cortexm',     // Pure JS Cortex-M emulator
    HARDWARE: 'hardware',   // Real USB MIDI hardware
    // Legacy alias
    WASM: 'umios',
};

/**
 * Unified backend interface
 *
 * All backends must implement these methods.
 * Subclasses should override abstract methods.
 */
export class BackendInterface {
    constructor() {
        /** @type {function|null} Message callback */
        this.onMessage = null;
        /** @type {function|null} Ready callback */
        this.onReady = null;
        /** @type {function|null} Error callback */
        this.onError = null;
    }

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /**
     * Start the backend
     * @returns {Promise<boolean>} Success
     */
    async start() {
        throw new Error('Not implemented');
    }

    /**
     * Stop the backend
     */
    stop() {
        throw new Error('Not implemented');
    }

    // =========================================================================
    // MIDI
    // =========================================================================

    /**
     * Send raw MIDI data
     * @param {Uint8Array|number[]} data - MIDI bytes
     */
    sendMidi(data) {
        throw new Error('Not implemented');
    }

    /**
     * Send Note On
     * @param {number} note - MIDI note number (0-127)
     * @param {number} velocity - Velocity (0-127)
     */
    noteOn(note, velocity) {
        throw new Error('Not implemented');
    }

    /**
     * Send Note Off
     * @param {number} note - MIDI note number (0-127)
     */
    noteOff(note) {
        throw new Error('Not implemented');
    }

    // =========================================================================
    // Parameters
    // =========================================================================

    /**
     * Set parameter value
     * @param {number} id - Parameter ID
     * @param {number} value - Parameter value
     */
    setParam(id, value) {
        throw new Error('Not implemented');
    }

    /**
     * Get backend state (async via onMessage)
     * @returns {object|null}
     */
    getState() {
        throw new Error('Not implemented');
    }

    // =========================================================================
    // Audio
    // =========================================================================

    /**
     * Get AudioContext
     * @returns {AudioContext|null}
     */
    getAudioContext() {
        return null;
    }

    /**
     * Get AnalyserNode for visualization
     * @returns {AnalyserNode|null}
     */
    getAnalyzer() {
        return null;
    }

    /**
     * Check if audio is playing
     * @returns {boolean}
     */
    isPlaying() {
        return false;
    }

    // =========================================================================
    // Extended methods (optional)
    // =========================================================================

    /**
     * Send shell command (UMI-OS backends)
     * @param {string} text - Command text
     */
    sendShellCommand(text) {
        // Optional - not all backends support this
    }

    /**
     * Apply hardware settings (UMI-OS backends)
     * @param {object} settings - Hardware settings
     */
    applyHwSettings(settings) {
        // Optional
    }

    /**
     * Reset DSP peak meters
     */
    resetDspPeak() {
        // Optional
    }

    /**
     * Reset backend to initial state
     */
    reset() {
        // Optional
    }

    /**
     * Get application info (generic backends)
     * @returns {object} { name, vendor, version }
     */
    getAppInfo() {
        return { name: 'Unknown', vendor: 'Unknown', version: '0.0.0' };
    }
}
