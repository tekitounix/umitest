/**
 * UMI Web - Core Module
 *
 * Audio backend management for UMI-OS web applications.
 *
 * @module umi_web/core
 *
 * @example
 * // ES Module import
 * import { BackendManager, BackendType, UmimBackend } from './lib/umi_web/core/index.js';
 *
 * // Create and start a backend
 * const manager = new BackendManager();
 * await manager.loadApplications('./apps.json');
 * const backend = await manager.switchToApp('umi-synth');
 * await backend.start();
 *
 * // Or use the singleton
 * import { backendManager } from './lib/umi_web/core/index.js';
 */

// Base types
export { BackendInterface, BackendType } from './backend.js';

// Protocol utilities (SysEx encoding/decoding)
export {
    UMI_SYSEX_ID,
    Command,
    encode7bit,
    decode7bit,
    calculateChecksum,
    buildMessage,
    parseMessage,
    parseShellOutput
} from './protocol.js';

// Backend implementations
export { UmimBackend, UmimGenericBackend, WasmBackend } from './backends/umim.js';
export { UmiosBackend, UmiosGenericBackend } from './backends/umios.js';
export { RenodeBackend, CortexMBackend } from './backends/renode.js';
export { HardwareBackend } from './backends/hardware.js';

// Manager
export { BackendManager, backendManager } from './manager.js';
