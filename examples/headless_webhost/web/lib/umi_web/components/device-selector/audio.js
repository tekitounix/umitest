/**
 * UMI Web - Audio Device Selector
 *
 * Manages audio input/output device selection.
 * Provides device enumeration, selection, and persistence.
 *
 * @module umi_web/audio/device-selector
 */

/**
 * Audio Device Selector
 *
 * Handles audio device enumeration and selection with localStorage persistence.
 */
export class AudioDeviceSelector {
    /**
     * @param {object} options
     * @param {string} [options.storageKeyOutput='umi-audio-output']
     * @param {string} [options.storageKeyInput='umi-audio-input']
     */
    constructor(options = {}) {
        this.storageKeyOutput = options.storageKeyOutput || 'umi-audio-output';
        this.storageKeyInput = options.storageKeyInput || 'umi-audio-input';

        /** @type {MediaDeviceInfo[]} */
        this.outputDevices = [];
        /** @type {MediaDeviceInfo[]} */
        this.inputDevices = [];

        /** @type {string|null} */
        this.selectedOutputId = null;
        /** @type {string|null} */
        this.selectedInputId = null;

        /** @type {MediaStream|null} */
        this.inputStream = null;
        /** @type {MediaStreamAudioSourceNode|null} */
        this.inputNode = null;

        // Callbacks
        /** @type {function|null} */
        this.onDevicesChanged = null;
        /** @type {function|null} */
        this.onOutputChanged = null;
        /** @type {function|null} */
        this.onInputChanged = null;
        /** @type {function|null} */
        this.onError = null;

        this._deviceChangeHandler = this._handleDeviceChange.bind(this);
    }

    /**
     * Check if audio output selection is supported
     * @returns {boolean}
     */
    static isOutputSelectionSupported() {
        return typeof AudioContext !== 'undefined' &&
               ('setSinkId' in AudioContext.prototype || 'sinkId' in AudioContext.prototype);
    }

    /**
     * Initialize device selector
     * @returns {Promise<boolean>}
     */
    async init() {
        try {
            // Request permission to enumerate devices
            await navigator.mediaDevices.getUserMedia({ audio: true })
                .then(stream => stream.getTracks().forEach(t => t.stop()))
                .catch(() => {}); // Ignore errors, we just need to trigger permission

            await this.updateDevices();

            // Listen for device changes
            navigator.mediaDevices.addEventListener('devicechange', this._deviceChangeHandler);

            // Restore saved selections
            this.selectedOutputId = this._loadSetting(this.storageKeyOutput);
            this.selectedInputId = this._loadSetting(this.storageKeyInput);

            return true;
        } catch (err) {
            if (this.onError) this.onError(err);
            return false;
        }
    }

    /**
     * Cleanup resources
     */
    destroy() {
        navigator.mediaDevices.removeEventListener('devicechange', this._deviceChangeHandler);
        this.disconnectInput();
    }

    /**
     * Update device lists
     * @returns {Promise<void>}
     */
    async updateDevices() {
        const devices = await navigator.mediaDevices.enumerateDevices();
        this.outputDevices = devices.filter(d => d.kind === 'audiooutput');
        this.inputDevices = devices.filter(d => d.kind === 'audioinput');

        if (this.onDevicesChanged) {
            this.onDevicesChanged({
                outputs: this.outputDevices,
                inputs: this.inputDevices
            });
        }
    }

    /**
     * Get output devices
     * @returns {MediaDeviceInfo[]}
     */
    getOutputDevices() {
        return this.outputDevices;
    }

    /**
     * Get input devices
     * @returns {MediaDeviceInfo[]}
     */
    getInputDevices() {
        return this.inputDevices;
    }

    /**
     * Select audio output device
     * @param {string} deviceId - Device ID (empty string for default)
     * @param {AudioContext} [audioContext] - AudioContext to apply selection
     * @returns {Promise<boolean>}
     */
    async selectOutput(deviceId, audioContext = null) {
        this.selectedOutputId = deviceId;
        this._saveSetting(this.storageKeyOutput, deviceId);

        if (audioContext && audioContext.setSinkId) {
            try {
                await audioContext.setSinkId(deviceId || '');
            } catch (err) {
                if (this.onError) this.onError(err);
                return false;
            }
        }

        if (this.onOutputChanged) {
            this.onOutputChanged(deviceId);
        }

        return true;
    }

    /**
     * Select audio input device and connect to AudioContext
     * @param {string} deviceId - Device ID
     * @param {AudioContext} audioContext - AudioContext to connect input
     * @returns {Promise<MediaStreamAudioSourceNode|null>}
     */
    async selectInput(deviceId, audioContext) {
        // Disconnect previous input
        this.disconnectInput();

        if (!deviceId) {
            this.selectedInputId = null;
            this._saveSetting(this.storageKeyInput, '');
            if (this.onInputChanged) this.onInputChanged(null);
            return null;
        }

        try {
            this.inputStream = await navigator.mediaDevices.getUserMedia({
                audio: { deviceId: { exact: deviceId } }
            });
            this.inputNode = audioContext.createMediaStreamSource(this.inputStream);
            this.selectedInputId = deviceId;
            this._saveSetting(this.storageKeyInput, deviceId);

            if (this.onInputChanged) {
                this.onInputChanged(this.inputNode);
            }

            return this.inputNode;
        } catch (err) {
            if (this.onError) this.onError(err);
            return null;
        }
    }

    /**
     * Disconnect audio input
     */
    disconnectInput() {
        if (this.inputNode) {
            this.inputNode.disconnect();
            this.inputNode = null;
        }
        if (this.inputStream) {
            this.inputStream.getTracks().forEach(t => t.stop());
            this.inputStream = null;
        }
    }

    /**
     * Get selected output device ID
     * @returns {string|null}
     */
    getSelectedOutputId() {
        return this.selectedOutputId;
    }

    /**
     * Get selected input device ID
     * @returns {string|null}
     */
    getSelectedInputId() {
        return this.selectedInputId;
    }

    /**
     * Get input node
     * @returns {MediaStreamAudioSourceNode|null}
     */
    getInputNode() {
        return this.inputNode;
    }

    /**
     * Apply saved output selection to AudioContext
     * @param {AudioContext} audioContext
     * @returns {Promise<boolean>}
     */
    async applyOutputSelection(audioContext) {
        if (this.selectedOutputId && audioContext.setSinkId) {
            return this.selectOutput(this.selectedOutputId, audioContext);
        }
        return true;
    }

    // Private methods

    _handleDeviceChange() {
        this.updateDevices();
    }

    _loadSetting(key) {
        try {
            return localStorage.getItem(key) || null;
        } catch {
            return null;
        }
    }

    _saveSetting(key, value) {
        try {
            if (value) {
                localStorage.setItem(key, value);
            } else {
                localStorage.removeItem(key);
            }
        } catch {}
    }
}

/**
 * Create a device selector dropdown element
 * @param {AudioDeviceSelector} selector
 * @param {'input'|'output'} type
 * @returns {HTMLSelectElement}
 */
export function createDeviceSelect(selector, type) {
    const select = document.createElement('select');
    select.className = 'audio-device-select';

    const updateOptions = () => {
        const devices = type === 'output'
            ? selector.getOutputDevices()
            : selector.getInputDevices();
        const selectedId = type === 'output'
            ? selector.getSelectedOutputId()
            : selector.getSelectedInputId();

        select.innerHTML = '';

        // Default option
        const defaultOpt = document.createElement('option');
        defaultOpt.value = '';
        defaultOpt.textContent = type === 'output' ? 'Default Output' : 'None';
        select.appendChild(defaultOpt);

        // Device options
        devices.forEach(device => {
            const opt = document.createElement('option');
            opt.value = device.deviceId;
            opt.textContent = device.label || `${type} ${devices.indexOf(device) + 1}`;
            if (device.deviceId === selectedId) {
                opt.selected = true;
            }
            select.appendChild(opt);
        });
    };

    selector.onDevicesChanged = updateOptions;
    updateOptions();

    return select;
}
