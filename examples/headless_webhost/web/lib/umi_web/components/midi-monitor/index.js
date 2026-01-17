/**
 * UMI Web - MIDI Monitor Component
 *
 * Real-time MIDI message display with filtering.
 *
 * @module umi_web/components/midi-monitor
 */

import { formatMidiMessage, parseMidiMessage } from '../device-selector/midi.js';

/**
 * MIDI Monitor component
 */
export class MidiMonitor {
    /**
     * @param {HTMLElement} container - Container element
     * @param {object} options
     * @param {number} [options.maxMessages=100]
     * @param {boolean} [options.showTimestamp=true]
     * @param {object} [options.filter] - Message type filters
     */
    constructor(container, options = {}) {
        this.container = container;
        this.maxMessages = options.maxMessages || 100;
        this.showTimestamp = options.showTimestamp !== false;

        // Filters (true = show)
        this.filter = {
            noteOn: true,
            noteOff: true,
            cc: true,
            programChange: true,
            pitchBend: true,
            aftertouch: true,
            sysex: true,
            clock: false,        // Often noisy
            activeSensing: false, // Very noisy
            ...options.filter
        };

        /** @type {Array<object>} Message history */
        this.messages = [];

        this._build();
    }

    /**
     * Add a MIDI message
     * @param {Uint8Array|number[]} data - MIDI data
     * @param {string} [direction='in'] - 'in' or 'out'
     */
    addMessage(data, direction = 'in') {
        const parsed = parseMidiMessage(data);

        // Apply filter
        if (!this._shouldShow(parsed)) return;

        const message = {
            timestamp: Date.now(),
            direction,
            data: Array.from(data),
            parsed,
            formatted: formatMidiMessage(data)
        };

        this.messages.push(message);

        // Trim old messages
        while (this.messages.length > this.maxMessages) {
            this.messages.shift();
        }

        this._addMessageElement(message);
    }

    /**
     * Clear all messages
     */
    clear() {
        this.messages = [];
        this.listEl.innerHTML = '';
    }

    /**
     * Set filter option
     * @param {string} type - Message type
     * @param {boolean} show - Show or hide
     */
    setFilter(type, show) {
        this.filter[type] = show;
    }

    /**
     * Get current filters
     * @returns {object}
     */
    getFilter() {
        return { ...this.filter };
    }

    /**
     * Export messages as text
     * @returns {string}
     */
    export() {
        return this.messages.map(m => {
            const time = new Date(m.timestamp).toISOString();
            const dir = m.direction === 'in' ? 'IN ' : 'OUT';
            return `${time} ${dir} ${m.formatted}`;
        }).join('\n');
    }

    /**
     * Destroy and cleanup
     */
    destroy() {
        this.container.innerHTML = '';
    }

    _build() {
        this.container.innerHTML = '';
        this.container.classList.add('umi-midi-monitor');

        this.listEl = document.createElement('div');
        this.listEl.className = 'midi-monitor-list';
        this.container.appendChild(this.listEl);
    }

    _shouldShow(parsed) {
        const type = parsed.type;

        // Special system messages
        if (parsed.status === 0xF8) return this.filter.clock;
        if (parsed.status === 0xFE) return this.filter.activeSensing;

        // Regular message types
        switch (type) {
            case 'noteOn':
            case 'noteOff':
                return this.filter.noteOn || this.filter.noteOff;
            case 'cc':
                return this.filter.cc;
            case 'programChange':
                return this.filter.programChange;
            case 'pitchBend':
                return this.filter.pitchBend;
            case 'aftertouch':
            case 'channelPressure':
                return this.filter.aftertouch;
            case 'sysex':
                return this.filter.sysex;
            default:
                return true;
        }
    }

    _addMessageElement(message) {
        const el = document.createElement('div');
        el.className = `midi-message ${message.direction}`;

        let html = '';

        if (this.showTimestamp) {
            const time = new Date(message.timestamp);
            const timeStr = time.toLocaleTimeString('en-US', {
                hour12: false,
                hour: '2-digit',
                minute: '2-digit',
                second: '2-digit'
            }) + '.' + String(time.getMilliseconds()).padStart(3, '0');
            html += `<span class="time">${timeStr}</span>`;
        }

        html += `<span class="dir">${message.direction === 'in' ? '←' : '→'}</span>`;
        html += `<span class="msg">${message.formatted}</span>`;

        el.innerHTML = html;
        this.listEl.appendChild(el);

        // Auto-scroll
        this.listEl.scrollTop = this.listEl.scrollHeight;

        // Remove old elements from DOM (keep in sync with messages array)
        while (this.listEl.children.length > this.maxMessages) {
            this.listEl.removeChild(this.listEl.firstChild);
        }
    }
}

// Re-export formatters from midi module
export { formatMidiMessage, parseMidiMessage, noteToName } from '../device-selector/midi.js';
