/**
 * UMI Web Library
 *
 * A modular library for building UMI-OS web applications.
 *
 * @module umi_web
 *
 * @example
 * // Import everything
 * import * as UmiWeb from './lib/umi_web/index.js';
 *
 * // Or import specific modules
 * import { BackendManager } from './lib/umi_web/core/index.js';
 * import { AudioDeviceSelector, MidiDeviceSelector } from './lib/umi_web/components/index.js';
 */

// Core - Backend management
export * from './core/index.js';

// Components - Functional components (logic + UI)
export * from './components/index.js';

// Theme - CSS variable-based theming
export * from './theme/index.js';

// Version
export const VERSION = '0.1.0';
