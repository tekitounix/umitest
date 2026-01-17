/**
 * UMI Web - Shell Component
 *
 * Terminal-like shell input/output for UMI-OS interaction.
 *
 * @module umi_web/components/shell
 */

/**
 * Shell component for UMI-OS terminal interaction
 */
export class Shell {
    /**
     * @param {HTMLElement} container - Container element
     * @param {object} options
     * @param {number} [options.maxHistory=100]
     * @param {number} [options.maxOutput=1000]
     * @param {string} [options.prompt='> ']
     */
    constructor(container, options = {}) {
        this.container = container;
        this.maxHistory = options.maxHistory || 100;
        this.maxOutput = options.maxOutput || 1000;
        this.prompt = options.prompt || '> ';

        /** @type {Array<string>} Command history */
        this.history = [];

        /** @type {number} Current history position */
        this.historyIndex = -1;

        /** @type {boolean} */
        this.enabled = false;

        // Callbacks
        /** @type {function|null} */
        this.onCommand = null;

        this._build();
    }

    /**
     * Enable shell input
     */
    enable() {
        this.enabled = true;
        this.inputEl.disabled = false;
        this.container.classList.remove('disabled');
    }

    /**
     * Disable shell input
     */
    disable() {
        this.enabled = false;
        this.inputEl.disabled = true;
        this.container.classList.add('disabled');
    }

    /**
     * Write output to shell
     * @param {string} text - Output text
     * @param {string} [type='stdout'] - 'stdout', 'stderr', 'system'
     */
    write(text, type = 'stdout') {
        const entry = document.createElement('div');
        entry.className = `shell-entry ${type}`;
        entry.textContent = text;
        this.outputEl.appendChild(entry);

        // Trim old entries
        while (this.outputEl.children.length > this.maxOutput) {
            this.outputEl.removeChild(this.outputEl.firstChild);
        }

        // Auto-scroll
        this.outputEl.scrollTop = this.outputEl.scrollHeight;
    }

    /**
     * Write command echo
     * @param {string} cmd
     */
    writeCommand(cmd) {
        this.write(this.prompt + cmd, 'command');
    }

    /**
     * Clear output
     */
    clear() {
        this.outputEl.innerHTML = '';
    }

    /**
     * Clear history
     */
    clearHistory() {
        this.history = [];
        this.historyIndex = -1;
    }

    /**
     * Focus input
     */
    focus() {
        this.inputEl.focus();
    }

    /**
     * Get command history
     * @returns {string[]}
     */
    getHistory() {
        return [...this.history];
    }

    /**
     * Destroy and cleanup
     */
    destroy() {
        this.container.innerHTML = '';
    }

    _build() {
        this.container.innerHTML = '';
        this.container.classList.add('umi-shell');

        // Output area
        this.outputEl = document.createElement('div');
        this.outputEl.className = 'shell-output';
        this.container.appendChild(this.outputEl);

        // Input area
        const inputWrapper = document.createElement('div');
        inputWrapper.className = 'shell-input-wrapper';

        const promptEl = document.createElement('span');
        promptEl.className = 'shell-prompt';
        promptEl.textContent = this.prompt;
        inputWrapper.appendChild(promptEl);

        this.inputEl = document.createElement('input');
        this.inputEl.type = 'text';
        this.inputEl.className = 'shell-input';
        this.inputEl.placeholder = 'Enter command...';
        this.inputEl.disabled = true;
        inputWrapper.appendChild(this.inputEl);

        this.container.appendChild(inputWrapper);

        // Event handlers
        this.inputEl.addEventListener('keydown', (e) => this._handleKeyDown(e));
    }

    _handleKeyDown(e) {
        if (e.key === 'Enter') {
            const cmd = this.inputEl.value.trim();
            if (cmd) {
                // Add to history
                this.history.push(cmd);
                if (this.history.length > this.maxHistory) {
                    this.history.shift();
                }
                this.historyIndex = this.history.length;

                // Echo command
                this.writeCommand(cmd);

                // Clear input
                this.inputEl.value = '';

                // Callback
                if (this.onCommand) {
                    this.onCommand(cmd);
                }
            }
        } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            if (this.historyIndex > 0) {
                this.historyIndex--;
                this.inputEl.value = this.history[this.historyIndex];
            }
        } else if (e.key === 'ArrowDown') {
            e.preventDefault();
            if (this.historyIndex < this.history.length - 1) {
                this.historyIndex++;
                this.inputEl.value = this.history[this.historyIndex];
            } else {
                this.historyIndex = this.history.length;
                this.inputEl.value = '';
            }
        } else if (e.key === 'c' && e.ctrlKey) {
            // Ctrl+C - cancel current input
            this.inputEl.value = '';
            this.write('^C', 'system');
        } else if (e.key === 'l' && e.ctrlKey) {
            // Ctrl+L - clear screen
            e.preventDefault();
            this.clear();
        }
    }
}

/**
 * Parse UMI-OS shell output from SysEx
 * @param {Uint8Array|number[]} data - SysEx data
 * @returns {object|null} { type: 'stdout'|'stderr', text: string }
 */
export function parseShellSysex(data) {
    // UMI-OS shell SysEx format: F0 00 21 27 [cmd] [7-bit encoded text] F7
    // cmd: 0x01 = stdout, 0x02 = stderr

    if (data.length < 6) return null;
    if (data[0] !== 0xF0 || data[data.length - 1] !== 0xF7) return null;

    // Check UMI manufacturer ID (example)
    if (data[1] !== 0x00 || data[2] !== 0x21 || data[3] !== 0x27) return null;

    const cmd = data[4];
    const payload = data.slice(5, -1);

    // Decode 7-bit to 8-bit
    const decoded = decode7bit(payload);
    const text = new TextDecoder().decode(new Uint8Array(decoded));

    if (cmd === 0x01) {
        return { type: 'stdout', text };
    } else if (cmd === 0x02) {
        return { type: 'stderr', text };
    }

    return null;
}

/**
 * Decode 7-bit MIDI-safe encoding to 8-bit
 * @param {Uint8Array|number[]} data
 * @returns {number[]}
 */
export function decode7bit(data) {
    const result = [];
    let i = 0;
    while (i < data.length) {
        const highBits = data[i++];
        for (let j = 0; j < 7 && i < data.length; j++) {
            const lowBits = data[i++];
            const byte = lowBits | ((highBits >> j) & 1) << 7;
            result.push(byte);
        }
    }
    return result;
}

/**
 * Encode 8-bit data to 7-bit MIDI-safe encoding
 * @param {Uint8Array|number[]} data
 * @returns {number[]}
 */
export function encode7bit(data) {
    const result = [];
    let i = 0;
    while (i < data.length) {
        let highBits = 0;
        const chunk = [];
        for (let j = 0; j < 7 && i < data.length; j++) {
            const byte = data[i++];
            highBits |= ((byte >> 7) & 1) << j;
            chunk.push(byte & 0x7F);
        }
        result.push(highBits);
        result.push(...chunk);
    }
    return result;
}

