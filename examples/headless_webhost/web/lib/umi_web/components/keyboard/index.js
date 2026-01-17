/**
 * UMI Web - Keyboard Component
 *
 * Virtual MIDI keyboard with mouse and computer keyboard support.
 *
 * @module umi_web/components/keyboard
 */

/**
 * Default key mappings (computer keyboard -> MIDI note)
 */
export const DEFAULT_KEY_MAP = {
    'a': { note: 48, label: 'C3' },
    'w': { note: 49, label: 'C#3' },
    's': { note: 50, label: 'D3' },
    'e': { note: 51, label: 'D#3' },
    'd': { note: 52, label: 'E3' },
    'f': { note: 53, label: 'F3' },
    't': { note: 54, label: 'F#3' },
    'g': { note: 55, label: 'G3' },
    'y': { note: 56, label: 'G#3' },
    'h': { note: 57, label: 'A3' },
    'u': { note: 58, label: 'A#3' },
    'j': { note: 59, label: 'B3' },
    'k': { note: 60, label: 'C4' },
    'o': { note: 61, label: 'C#4' },
    'l': { note: 62, label: 'D4' },
    'p': { note: 63, label: 'D#4' },
    ';': { note: 64, label: 'E4' },
};

/**
 * Default note layout for visual keyboard
 */
export const DEFAULT_NOTES = [
    { note: 48, label: 'C3', black: false },
    { note: 49, label: 'C#', black: true },
    { note: 50, label: 'D3', black: false },
    { note: 51, label: 'D#', black: true },
    { note: 52, label: 'E3', black: false },
    { note: 53, label: 'F3', black: false },
    { note: 54, label: 'F#', black: true },
    { note: 55, label: 'G3', black: false },
    { note: 56, label: 'G#', black: true },
    { note: 57, label: 'A3', black: false },
    { note: 58, label: 'A#', black: true },
    { note: 59, label: 'B3', black: false },
    { note: 60, label: 'C4', black: false },
    { note: 61, label: 'C#', black: true },
    { note: 62, label: 'D4', black: false },
    { note: 63, label: 'D#', black: true },
    { note: 64, label: 'E4', black: false },
];

/**
 * Virtual MIDI keyboard component
 */
export class Keyboard {
    /**
     * @param {HTMLElement} container - Container element for keyboard
     * @param {object} options
     * @param {Array} [options.notes] - Note definitions
     * @param {object} [options.keyMap] - Computer keyboard mappings
     * @param {number} [options.velocity=100] - Default velocity
     */
    constructor(container, options = {}) {
        this.container = container;
        this.notes = options.notes || DEFAULT_NOTES;
        this.keyMap = options.keyMap || DEFAULT_KEY_MAP;
        this.velocity = options.velocity || 100;

        /** @type {Set<number>} Active notes */
        this.activeNotes = new Set();

        /** @type {Map<number, HTMLElement>} Note -> key element */
        this.keyElements = new Map();

        // Callbacks
        /** @type {function|null} */
        this.onNoteOn = null;
        /** @type {function|null} */
        this.onNoteOff = null;

        this._boundKeyDown = this._handleKeyDown.bind(this);
        this._boundKeyUp = this._handleKeyUp.bind(this);

        this._build();
    }

    /**
     * Enable keyboard input
     */
    enable() {
        document.addEventListener('keydown', this._boundKeyDown);
        document.addEventListener('keyup', this._boundKeyUp);
    }

    /**
     * Disable keyboard input
     */
    disable() {
        document.removeEventListener('keydown', this._boundKeyDown);
        document.removeEventListener('keyup', this._boundKeyUp);
    }

    /**
     * Trigger note on
     * @param {number} note
     * @param {number} [velocity]
     */
    noteOn(note, velocity = this.velocity) {
        if (this.activeNotes.has(note)) return;

        this.activeNotes.add(note);
        this._setKeyActive(note, true);

        if (this.onNoteOn) {
            this.onNoteOn(note, velocity);
        }
    }

    /**
     * Trigger note off
     * @param {number} note
     */
    noteOff(note) {
        if (!this.activeNotes.has(note)) return;

        this.activeNotes.delete(note);
        this._setKeyActive(note, false);

        if (this.onNoteOff) {
            this.onNoteOff(note);
        }
    }

    /**
     * Release all notes
     */
    allNotesOff() {
        for (const note of this.activeNotes) {
            this.noteOff(note);
        }
    }

    /**
     * Get active notes
     * @returns {number[]}
     */
    getActiveNotes() {
        return Array.from(this.activeNotes);
    }

    /**
     * Destroy and cleanup
     */
    destroy() {
        this.disable();
        this.allNotesOff();
        this.container.innerHTML = '';
        this.keyElements.clear();
    }

    _build() {
        this.container.innerHTML = '';
        this.container.classList.add('umi-keyboard');

        for (const n of this.notes) {
            const key = document.createElement('div');
            key.className = 'key ' + (n.black ? 'black' : 'white');
            key.dataset.note = n.note;

            if (!n.black) {
                const label = document.createElement('span');
                label.className = 'key-label';
                label.textContent = n.label;
                key.appendChild(label);
            }

            // Mouse events
            key.addEventListener('mousedown', (e) => {
                e.preventDefault();
                this.noteOn(n.note);
            });
            key.addEventListener('mouseup', () => {
                this.noteOff(n.note);
            });
            key.addEventListener('mouseleave', () => {
                if (this.activeNotes.has(n.note)) {
                    this.noteOff(n.note);
                }
            });

            // Touch events
            key.addEventListener('touchstart', (e) => {
                e.preventDefault();
                this.noteOn(n.note);
            });
            key.addEventListener('touchend', () => {
                this.noteOff(n.note);
            });

            this.container.appendChild(key);
            this.keyElements.set(n.note, key);
        }
    }

    _setKeyActive(note, active) {
        const key = this.keyElements.get(note);
        if (key) {
            if (active) {
                key.classList.add('active');
            } else {
                key.classList.remove('active');
            }
        }
    }

    _handleKeyDown(e) {
        if (e.repeat) return;

        // Ignore if focused on input element
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;

        const info = this.keyMap[e.key.toLowerCase()];
        if (info) {
            e.preventDefault();
            this.noteOn(info.note);
        }
    }

    _handleKeyUp(e) {
        const info = this.keyMap[e.key.toLowerCase()];
        if (info) {
            this.noteOff(info.note);
        }
    }
}
