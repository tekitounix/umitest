/**
 * UMI Web - Parameter Control Component
 *
 * Parameter sliders with MIDI Learn support.
 *
 * @module umi_web/components/param-control
 */

/**
 * Parameter control component with MIDI Learn
 */
export class ParamControl {
    /**
     * @param {HTMLElement} container - Container element
     * @param {object} options
     * @param {string} [options.storageKey='umi-midi-mappings']
     */
    constructor(container, options = {}) {
        this.container = container;
        this.storageKey = options.storageKey || 'umi-midi-mappings';

        /** @type {Array<object>} Parameter definitions */
        this.params = [];

        /** @type {Map<number, HTMLElement>} param id -> element */
        this.paramElements = new Map();

        /** @type {Map<string, number>} "ch:cc" -> param id */
        this.midiMappings = new Map();

        /** @type {number|null} Currently learning param id */
        this.learnTarget = null;

        /** @type {boolean} */
        this.learnMode = false;

        // Callbacks
        /** @type {function|null} */
        this.onParamChange = null;
        /** @type {function|null} */
        this.onLearnStart = null;
        /** @type {function|null} */
        this.onLearnComplete = null;

        this._loadMappings();
    }

    /**
     * Set parameters and build UI
     * @param {Array<object>} params - Parameter definitions
     */
    setParams(params) {
        this.params = params;
        this._build();
    }

    /**
     * Get current parameter values
     * @returns {Map<number, number>}
     */
    getValues() {
        const values = new Map();
        for (const p of this.params) {
            const slider = this.container.querySelector(`#param-${p.id}`);
            if (slider) {
                values.set(p.id, parseFloat(slider.value));
            }
        }
        return values;
    }

    /**
     * Set parameter value
     * @param {number} id - Parameter ID
     * @param {number} value - Value
     */
    setValue(id, value) {
        const slider = this.container.querySelector(`#param-${id}`);
        if (slider) {
            slider.value = value;
            this._updateValueDisplay(id, value);
        }
    }

    /**
     * Start MIDI learn mode
     */
    startLearn() {
        this.learnMode = true;
        this.container.classList.add('midi-learn-mode');
        if (this.onLearnStart) this.onLearnStart();
    }

    /**
     * Stop MIDI learn mode
     */
    stopLearn() {
        this.learnMode = false;
        this.learnTarget = null;
        this.container.classList.remove('midi-learn-mode');

        // Remove highlight from all params
        this.paramElements.forEach(el => el.classList.remove('learn-target'));
    }

    /**
     * Select parameter for MIDI learn
     * @param {number} paramId
     */
    selectLearnTarget(paramId) {
        if (!this.learnMode) return;

        // Remove previous highlight
        this.paramElements.forEach(el => el.classList.remove('learn-target'));

        this.learnTarget = paramId;
        const el = this.paramElements.get(paramId);
        if (el) {
            el.classList.add('learn-target');
        }
    }

    /**
     * Handle incoming MIDI CC for learn/control
     * @param {number} channel - MIDI channel (0-15)
     * @param {number} cc - CC number
     * @param {number} value - CC value (0-127)
     * @returns {boolean} True if handled
     */
    handleCC(channel, cc, value) {
        const key = `${channel}:${cc}`;

        // Learn mode: assign CC to current target
        if (this.learnMode && this.learnTarget !== null) {
            // Remove old mapping for this CC
            for (const [k, id] of this.midiMappings) {
                if (k === key) {
                    this.midiMappings.delete(k);
                    this._updateMappingDisplay(id, null);
                }
            }

            // Remove old mapping for this param
            for (const [k, id] of this.midiMappings) {
                if (id === this.learnTarget) {
                    this.midiMappings.delete(k);
                }
            }

            // Set new mapping
            this.midiMappings.set(key, this.learnTarget);
            this._updateMappingDisplay(this.learnTarget, { channel, cc });
            this._saveMappings();

            if (this.onLearnComplete) {
                this.onLearnComplete(this.learnTarget, channel, cc);
            }

            // Clear target but stay in learn mode
            const el = this.paramElements.get(this.learnTarget);
            if (el) el.classList.remove('learn-target');
            this.learnTarget = null;

            return true;
        }

        // Normal mode: apply CC to mapped parameter
        const paramId = this.midiMappings.get(key);
        if (paramId !== undefined) {
            const param = this.params.find(p => p.id === paramId);
            if (param) {
                // Map 0-127 to param range
                const normalized = value / 127;
                const paramValue = param.min + normalized * (param.max - param.min);
                this.setValue(paramId, paramValue);

                if (this.onParamChange) {
                    this.onParamChange(paramId, paramValue);
                }
                return true;
            }
        }

        return false;
    }

    /**
     * Clear mapping for parameter
     * @param {number} paramId
     */
    clearMapping(paramId) {
        for (const [key, id] of this.midiMappings) {
            if (id === paramId) {
                this.midiMappings.delete(key);
                break;
            }
        }
        this._updateMappingDisplay(paramId, null);
        this._saveMappings();
    }

    /**
     * Clear all mappings
     */
    clearAllMappings() {
        this.midiMappings.clear();
        this.paramElements.forEach((el, id) => {
            this._updateMappingDisplay(id, null);
        });
        this._saveMappings();
    }

    /**
     * Destroy and cleanup
     */
    destroy() {
        this.stopLearn();
        this.container.innerHTML = '';
        this.paramElements.clear();
    }

    _build() {
        this.container.innerHTML = '';
        this.container.classList.add('umi-param-control');
        this.paramElements.clear();

        for (const p of this.params) {
            const initialValue = p.default !== undefined ? p.default : p.min;

            const div = document.createElement('div');
            div.className = 'param';
            div.dataset.paramId = p.id;

            div.innerHTML = `
                <div class="param-label">
                    <span class="param-name">${p.name}</span>
                    <span class="param-value" id="pval-${p.id}">${this._formatValue(p, initialValue)}</span>
                </div>
                <input type="range"
                       id="param-${p.id}"
                       min="${p.min}"
                       max="${p.max}"
                       step="${(p.max - p.min) / 100}"
                       value="${initialValue}">
                <div class="param-mapping" id="pmap-${p.id}"></div>
            `;

            this.container.appendChild(div);
            this.paramElements.set(p.id, div);

            // Slider change handler
            const slider = div.querySelector('input');
            slider.addEventListener('input', () => {
                const value = parseFloat(slider.value);
                this._updateValueDisplay(p.id, value);
                if (this.onParamChange) {
                    this.onParamChange(p.id, value);
                }
            });

            // Click handler for MIDI learn
            div.addEventListener('click', (e) => {
                if (this.learnMode && e.target !== slider) {
                    this.selectLearnTarget(p.id);
                }
            });

            // Restore mapping display
            this._restoreMappingDisplay(p.id);
        }
    }

    _formatValue(param, value) {
        const name = param.name.toLowerCase();

        if (name.includes('sustain') || name.includes('resonance') || name.includes('volume')) {
            return (value * 100).toFixed(0) + '%';
        } else if (name.includes('cutoff') || name.includes('freq')) {
            return value >= 1000 ? (value / 1000).toFixed(1) + 'k' : value.toFixed(0) + ' Hz';
        } else if (name.includes('attack') || name.includes('decay') || name.includes('release')) {
            return value.toFixed(0) + ' ms';
        } else if (param.unit) {
            return value.toFixed(1) + ' ' + param.unit;
        } else {
            return value.toFixed(2);
        }
    }

    _updateValueDisplay(paramId, value) {
        const param = this.params.find(p => p.id === paramId);
        const el = this.container.querySelector(`#pval-${paramId}`);
        if (param && el) {
            el.textContent = this._formatValue(param, value);
        }
    }

    _updateMappingDisplay(paramId, mapping) {
        const el = this.container.querySelector(`#pmap-${paramId}`);
        if (!el) return;

        if (mapping) {
            el.innerHTML = `CC${mapping.cc} (ch${mapping.channel + 1}) <span class="clear-btn" data-param="${paramId}">[×]</span>`;
            el.querySelector('.clear-btn')?.addEventListener('click', (e) => {
                e.stopPropagation();
                this.clearMapping(paramId);
            });
        } else {
            el.innerHTML = '';
        }
    }

    _restoreMappingDisplay(paramId) {
        for (const [key, id] of this.midiMappings) {
            if (id === paramId) {
                const [ch, cc] = key.split(':').map(Number);
                this._updateMappingDisplay(paramId, { channel: ch, cc });
                break;
            }
        }
    }

    _loadMappings() {
        try {
            const saved = localStorage.getItem(this.storageKey);
            if (saved) {
                const data = JSON.parse(saved);
                this.midiMappings = new Map(Object.entries(data).map(([k, v]) => [k, v]));
            }
        } catch {}
    }

    _saveMappings() {
        try {
            const data = Object.fromEntries(this.midiMappings);
            localStorage.setItem(this.storageKey, JSON.stringify(data));
        } catch {}
    }
}

