/**
 * UMI Web - Backend Manager
 *
 * Factory and coordinator for audio backends.
 * Handles application loading, backend switching, and lifecycle management.
 *
 * @module umi_web/core/manager
 */

import { BackendType } from './backend.js';
import { UmimBackend, UmimGenericBackend } from './backends/umim.js';
import { UmiosBackend, UmiosGenericBackend } from './backends/umios.js';
import { RenodeBackend, CortexMBackend } from './backends/renode.js';
import { HardwareBackend } from './backends/hardware.js';

/**
 * Backend Manager - Factory and coordinator
 *
 * Manages multiple simulation backends with unified interface.
 * Supports application manifest loading and dynamic backend switching.
 */
export class BackendManager {
    constructor() {
        /** @type {BackendInterface|null} */
        this.currentBackend = null;
        /** @type {string|null} */
        this.currentType = null;
        /** @type {object|null} */
        this.currentApp = null;
        /** @type {Array<object>} */
        this.applications = [];
    }

    /**
     * Load applications from manifest
     * @param {string} [manifestUrl='./apps.json']
     * @returns {Promise<Array<object>>}
     */
    async loadApplications(manifestUrl = './apps.json') {
        try {
            const response = await fetch(manifestUrl + '?v=' + Date.now());
            if (!response.ok) {
                console.warn('[BackendManager] Failed to load apps.json:', response.status);
                return [];
            }
            const manifest = await response.json();
            this.applications = manifest.applications || [];
            console.log('[BackendManager] Loaded', this.applications.length, 'applications');
            return this.applications;
        } catch (err) {
            console.warn('[BackendManager] Error loading apps.json:', err);
            return [];
        }
    }

    /**
     * Get loaded applications
     * @returns {Array<object>}
     */
    getApplications() {
        return this.applications;
    }

    /**
     * Get application by ID
     * @param {string} appId
     * @returns {object|undefined}
     */
    getApplication(appId) {
        return this.applications.find(app => app.id === appId);
    }

    /**
     * Get default application
     * @returns {object|undefined}
     */
    getDefaultApplication() {
        return this.applications.find(app => app.default) || this.applications[0];
    }

    /**
     * Get available backends with their status
     * @returns {Promise<Array<object>>}
     */
    async getAvailableBackends() {
        const backends = [
            {
                type: BackendType.UMIM,
                name: 'UMIM (DSP)',
                description: 'DSP layer only, fast real-time audio',
                available: true,
                isSimulator: true,
            },
            {
                type: BackendType.UMIOS,
                name: 'UMI-OS (Kernel)',
                description: 'Full UMI-OS kernel simulation with task scheduler',
                available: true,
                isSimulator: true,
            },
            {
                type: BackendType.RENODE,
                name: 'Renode (HW)',
                description: 'Cycle-accurate hardware simulation, requires Renode server',
                available: false,
                isSimulator: true,
            },
            {
                type: 'hardware',
                name: 'USB Hardware',
                description: 'Connect to real UMI device via USB MIDI',
                available: false,
                isSimulator: false,
            },
        ];

        // Check Renode availability (doesn't require permission)
        backends[2].available = await RenodeBackend.isAvailable();

        // Hardware availability is checked on-demand via getHardwareDevices()
        // to avoid triggering MIDI permission dialog on page load
        backends[3].available = HardwareBackend.isSupported();

        return backends;
    }

    /**
     * Get available hardware devices
     * @returns {Promise<Array<{name: string}>>}
     */
    async getHardwareDevices() {
        const backend = new HardwareBackend();
        return backend.getDevices();
    }

    /**
     * Create backend of specified type
     * @param {string} type - Backend type (umim, umios, renode, hardware, etc.)
     * @param {object} [options={}]
     * @returns {BackendInterface}
     */
    createBackend(type, options = {}) {
        switch (type) {
            case 'umim':
                return new UmimBackend(options);
            case 'umim-generic':
                return new UmimGenericBackend(options);
            case 'umios':
                return new UmiosBackend(options);
            case 'umios-generic':
                return new UmiosGenericBackend(options);
            case 'renode':
                return new RenodeBackend(options);
            case 'cortexm':
                return new CortexMBackend(options);
            case 'hardware':
                return new HardwareBackend(options);
            default:
                throw new Error(`Unknown backend type: ${type}`);
        }
    }

    /**
     * Create backend from application config
     * @param {object} app - Application config from manifest
     * @param {object} [options={}]
     * @returns {BackendInterface}
     */
    createBackendFromApp(app, options = {}) {
        const backendType = app.backend;
        const backendOptions = {
            wasmUrl: app.wasmUrl,
            workletUrl: app.workletUrl,
            appInfo: {
                name: app.name,
                vendor: app.vendor || 'Unknown',
                version: app.version || '0.0.0'
            },
            ...options
        };

        // Use generic backends for dynamic WASM loading
        if (backendType === 'umim') {
            return new UmimGenericBackend(backendOptions);
        } else if (backendType === 'umios') {
            return new UmiosGenericBackend(backendOptions);
        } else {
            return this.createBackend(backendType, backendOptions);
        }
    }

    /**
     * Switch to application by ID
     * @param {string} appId
     * @param {object} [options={}]
     * @returns {Promise<BackendInterface>}
     */
    async switchToApp(appId, options = {}) {
        const app = this.getApplication(appId);
        if (!app) {
            throw new Error(`Application not found: ${appId}`);
        }

        // Stop current backend
        if (this.currentBackend) {
            this.currentBackend.stop();
            this.currentBackend = null;
        }

        // Create new backend from app config
        this.currentBackend = this.createBackendFromApp(app, options);
        this.currentType = app.backend;
        this.currentApp = app;

        return this.currentBackend;
    }

    /**
     * Switch to a different backend type
     * @param {string} type - Backend type
     * @param {object} [options={}]
     * @returns {Promise<BackendInterface>}
     */
    async switchBackend(type, options = {}) {
        // Stop current backend
        if (this.currentBackend) {
            this.currentBackend.stop();
            this.currentBackend = null;
        }

        // Create new backend
        this.currentBackend = this.createBackend(type, options);
        this.currentType = type;
        this.currentApp = null;

        return this.currentBackend;
    }

    /**
     * Get current backend
     * @returns {BackendInterface|null}
     */
    getBackend() {
        return this.currentBackend;
    }

    /**
     * Get current backend type
     * @returns {string|null}
     */
    getType() {
        return this.currentType;
    }

    /**
     * Get current application
     * @returns {object|null}
     */
    getCurrentApp() {
        return this.currentApp;
    }

    /**
     * Stop and cleanup current backend
     */
    stop() {
        if (this.currentBackend) {
            this.currentBackend.stop();
            this.currentBackend = null;
            this.currentType = null;
            this.currentApp = null;
        }
    }
}

// Export singleton instance
export const backendManager = new BackendManager();
