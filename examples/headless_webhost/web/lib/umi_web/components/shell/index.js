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
        // For stdout/stderr, append to current pending entry if same type
        // Only flush when we see a newline at the end
        if (type === 'stdout' || type === 'stderr') {
            if (!this._pendingEntry || this._pendingType !== type) {
                // Create new entry
                this._flushPending();
                this._pendingEntry = document.createElement('pre');
                this._pendingEntry.className = `shell-entry ${type}`;
                this._pendingEntry.style.margin = '0';
                this._pendingEntry.style.whiteSpace = 'pre-wrap';
                this._pendingType = type;
                this.outputEl.appendChild(this._pendingEntry);
            }
            this._pendingEntry.textContent += text;

            // If text ends with newline, flush
            if (text.endsWith('\n')) {
                this._flushPending();
            }
        } else {
            // System/command messages: flush pending and write immediately
            this._flushPending();
            const entry = document.createElement('div');
            entry.className = `shell-entry ${type}`;
            entry.textContent = text;
            this.outputEl.appendChild(entry);
        }

        // Trim old entries
        while (this.outputEl.children.length > this.maxOutput) {
            this.outputEl.removeChild(this.outputEl.firstChild);
        }

        // Auto-scroll
        this.outputEl.scrollTop = this.outputEl.scrollHeight;
    }

    /**
     * Flush pending output entry
     * @private
     */
    _flushPending() {
        this._pendingEntry = null;
        this._pendingType = null;
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

// Re-export protocol utilities for backwards compatibility
export {
    encode7bit,
    decode7bit,
    parseShellOutput as parseShellSysex
} from '../../core/protocol.js';

