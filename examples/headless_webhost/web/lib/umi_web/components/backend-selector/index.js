/**
 * UMI Web - Backend Selector Component
 *
 * UI for selecting between simulator and hardware backends.
 * Shows available backends and hardware devices.
 *
 * @module umi_web/components/backend-selector
 */

import { backendManager } from '../../core/manager.js';

/**
 * Backend Selector Component
 *
 * Creates a dropdown/modal UI for backend selection.
 */
export class BackendSelector {
    /**
     * @param {object} options
     * @param {HTMLElement} options.container - Container element
     * @param {function} [options.onBackendChange] - Callback when backend changes
     */
    constructor(options = {}) {
        this.container = options.container;
        this.onBackendChange = options.onBackendChange || null;

        this.backends = [];
        this.hardwareDevices = [];
        this.selectedBackend = null;
        this.selectedDevice = null;

        this._createUI();
    }

    /**
     * Create the UI elements
     * @private
     */
    _createUI() {
        this.container.innerHTML = `
            <div class="backend-selector">
                <div class="backend-selector-header">
                    <span class="backend-selector-label">Target:</span>
                    <button class="backend-selector-btn" type="button">
                        <span class="backend-status-dot"></span>
                        <span class="backend-name">Loading...</span>
                        <span class="backend-chevron">&#9662;</span>
                    </button>
                </div>
                <div class="backend-dropdown" style="display: none;">
                    <div class="backend-section">
                        <div class="backend-section-title">Simulators</div>
                        <div class="backend-list simulator-list"></div>
                    </div>
                    <div class="backend-section">
                        <div class="backend-section-title">Hardware</div>
                        <div class="backend-list hardware-list"></div>
                        <button class="backend-refresh-btn" type="button">Refresh Devices</button>
                    </div>
                </div>
            </div>
        `;

        this.button = this.container.querySelector('.backend-selector-btn');
        this.nameSpan = this.container.querySelector('.backend-name');
        this.statusDot = this.container.querySelector('.backend-status-dot');
        this.dropdown = this.container.querySelector('.backend-dropdown');
        this.simulatorList = this.container.querySelector('.simulator-list');
        this.hardwareList = this.container.querySelector('.hardware-list');
        this.refreshBtn = this.container.querySelector('.backend-refresh-btn');

        // Event listeners
        this.button.addEventListener('click', () => this._toggleDropdown());
        this.refreshBtn.addEventListener('click', () => this.refresh());

        // Close dropdown on outside click
        document.addEventListener('click', (e) => {
            if (!this.container.contains(e.target)) {
                this._hideDropdown();
            }
        });

        // Add styles
        this._addStyles();
    }

    /**
     * Add component styles
     * @private
     */
    _addStyles() {
        if (document.getElementById('backend-selector-styles')) return;

        const style = document.createElement('style');
        style.id = 'backend-selector-styles';
        style.textContent = `
            .backend-selector {
                position: relative;
                font-size: 12px;
            }
            .backend-selector-header {
                display: flex;
                align-items: center;
                gap: 8px;
            }
            .backend-selector-label {
                color: var(--umi-text-secondary, #888);
            }
            .backend-selector-btn {
                display: flex;
                align-items: center;
                gap: 6px;
                padding: 4px 10px;
                background: var(--umi-bg-secondary, #1a1a2e);
                border: 1px solid var(--umi-border, #252538);
                border-radius: 4px;
                color: var(--umi-text-primary, #fff);
                cursor: pointer;
                font-size: 12px;
                min-width: 140px;
            }
            .backend-selector-btn:hover {
                border-color: var(--umi-accent, #4ecca3);
            }
            .backend-status-dot {
                width: 8px;
                height: 8px;
                border-radius: 50%;
                background: var(--umi-text-muted, #555);
            }
            .backend-status-dot.connected {
                background: var(--umi-success, #4ecca3);
            }
            .backend-status-dot.error {
                background: var(--umi-error, #e74c3c);
            }
            .backend-chevron {
                margin-left: auto;
                font-size: 10px;
                color: var(--umi-text-secondary, #888);
            }
            .backend-dropdown {
                position: absolute;
                top: 100%;
                left: 0;
                right: 0;
                min-width: 220px;
                margin-top: 4px;
                background: var(--umi-bg-secondary, #1a1a2e);
                border: 1px solid var(--umi-border, #252538);
                border-radius: 6px;
                box-shadow: 0 4px 12px rgba(0,0,0,0.3);
                z-index: 1000;
                overflow: hidden;
            }
            .backend-section {
                padding: 8px;
            }
            .backend-section + .backend-section {
                border-top: 1px solid var(--umi-border, #252538);
            }
            .backend-section-title {
                font-size: 10px;
                font-weight: 600;
                color: var(--umi-text-secondary, #888);
                text-transform: uppercase;
                margin-bottom: 6px;
                padding: 0 4px;
            }
            .backend-list {
                display: flex;
                flex-direction: column;
                gap: 2px;
            }
            .backend-item {
                display: flex;
                align-items: center;
                gap: 8px;
                padding: 6px 8px;
                border-radius: 4px;
                cursor: pointer;
                transition: background 0.1s;
            }
            .backend-item:hover {
                background: var(--umi-bg-tertiary, #252538);
            }
            .backend-item.selected {
                background: var(--umi-accent, #4ecca3);
                color: var(--umi-bg-primary, #0a0a15);
            }
            .backend-item.disabled {
                opacity: 0.4;
                cursor: not-allowed;
            }
            .backend-item-dot {
                width: 6px;
                height: 6px;
                border-radius: 50%;
                background: var(--umi-text-muted, #555);
            }
            .backend-item.available .backend-item-dot {
                background: var(--umi-success, #4ecca3);
            }
            .backend-item-name {
                flex: 1;
            }
            .backend-item-desc {
                font-size: 10px;
                color: var(--umi-text-muted, #666);
            }
            .backend-refresh-btn {
                width: 100%;
                margin-top: 6px;
                padding: 6px;
                background: transparent;
                border: 1px dashed var(--umi-border, #252538);
                border-radius: 4px;
                color: var(--umi-text-secondary, #888);
                font-size: 11px;
                cursor: pointer;
            }
            .backend-refresh-btn:hover {
                border-color: var(--umi-accent, #4ecca3);
                color: var(--umi-accent, #4ecca3);
            }
            .backend-empty {
                padding: 8px;
                text-align: center;
                color: var(--umi-text-muted, #666);
                font-size: 11px;
            }
        `;
        document.head.appendChild(style);
    }

    /**
     * Initialize and load available backends
     * @returns {Promise<void>}
     */
    async init() {
        await this.refresh();

        // Set default selection
        if (!this.selectedBackend && this.backends.length > 0) {
            const defaultBackend = this.backends.find(b => b.available && b.isSimulator);
            if (defaultBackend) {
                this._selectBackend(defaultBackend.type, null);
            }
        }
    }

    /**
     * Refresh available backends and devices
     * @returns {Promise<void>}
     */
    async refresh() {
        // Get available backends
        this.backends = await backendManager.getAvailableBackends();

        // Get hardware devices
        try {
            this.hardwareDevices = await backendManager.getHardwareDevices();
        } catch (e) {
            console.warn('[BackendSelector] Could not get hardware devices:', e);
            this.hardwareDevices = [];
        }

        this._renderLists();
    }

    /**
     * Render the backend lists
     * @private
     */
    _renderLists() {
        // Render simulators
        const simulators = this.backends.filter(b => b.isSimulator);
        this.simulatorList.innerHTML = simulators.map(b => `
            <div class="backend-item ${b.available ? 'available' : 'disabled'} ${this.selectedBackend === b.type ? 'selected' : ''}"
                 data-type="${b.type}"
                 title="${b.description}">
                <span class="backend-item-dot"></span>
                <span class="backend-item-name">${b.name}</span>
            </div>
        `).join('');

        // Render hardware devices
        if (this.hardwareDevices.length > 0) {
            this.hardwareList.innerHTML = this.hardwareDevices.map(d => `
                <div class="backend-item available ${this.selectedBackend === 'hardware' && this.selectedDevice === d.name ? 'selected' : ''}"
                     data-type="hardware"
                     data-device="${d.name}">
                    <span class="backend-item-dot"></span>
                    <span class="backend-item-name">${d.name}</span>
                </div>
            `).join('');
        } else {
            this.hardwareList.innerHTML = `
                <div class="backend-empty">No UMI devices found</div>
            `;
        }

        // Add click handlers
        this.simulatorList.querySelectorAll('.backend-item:not(.disabled)').forEach(el => {
            el.addEventListener('click', () => {
                this._selectBackend(el.dataset.type, null);
                this._hideDropdown();
            });
        });

        this.hardwareList.querySelectorAll('.backend-item:not(.disabled)').forEach(el => {
            el.addEventListener('click', () => {
                this._selectBackend(el.dataset.type, el.dataset.device);
                this._hideDropdown();
            });
        });
    }

    /**
     * Select a backend
     * @private
     */
    _selectBackend(type, deviceName) {
        this.selectedBackend = type;
        this.selectedDevice = deviceName;

        // Update button text
        if (type === 'hardware' && deviceName) {
            this.nameSpan.textContent = deviceName;
        } else {
            const backend = this.backends.find(b => b.type === type);
            this.nameSpan.textContent = backend ? backend.name : type;
        }

        // Update status dot
        this.statusDot.classList.remove('connected', 'error');

        // Re-render lists to show selection
        this._renderLists();

        // Notify callback
        if (this.onBackendChange) {
            this.onBackendChange({
                type,
                deviceName,
                isSimulator: type !== 'hardware'
            });
        }
    }

    /**
     * Set connection status
     * @param {'connected'|'disconnected'|'error'} status
     */
    setStatus(status) {
        this.statusDot.classList.remove('connected', 'error');
        if (status === 'connected') {
            this.statusDot.classList.add('connected');
        } else if (status === 'error') {
            this.statusDot.classList.add('error');
        }
    }

    /**
     * Get current selection
     * @returns {{type: string, deviceName: string|null, isSimulator: boolean}}
     */
    getSelection() {
        return {
            type: this.selectedBackend,
            deviceName: this.selectedDevice,
            isSimulator: this.selectedBackend !== 'hardware'
        };
    }

    /**
     * Toggle dropdown visibility
     * @private
     */
    _toggleDropdown() {
        if (this.dropdown.style.display === 'none') {
            this.dropdown.style.display = 'block';
        } else {
            this._hideDropdown();
        }
    }

    /**
     * Hide dropdown
     * @private
     */
    _hideDropdown() {
        this.dropdown.style.display = 'none';
    }
}
