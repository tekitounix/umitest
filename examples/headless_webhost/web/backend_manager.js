/**
 * UMI-OS Backend Manager - Compatibility Layer
 *
 * This file re-exports from the modular lib/umi_web/core library
 * for backward compatibility with existing code.
 *
 * New code should import directly from:
 *   import { BackendManager, ... } from './lib/umi_web/core/index.js';
 *
 * Architecture:
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                    BackendManager                                   │
 * │  ┌─────────────────────────────────────────────────────────────┐   │
 * │  │                 Unified API                                  │   │
 * │  │  - start()     - stop()      - sendMidi()                   │   │
 * │  │  - setParam()  - getState()  - onMessage()                  │   │
 * │  └─────────────────────────────────────────────────────────────┘   │
 * │                              │                                      │
 * │         ┌────────────────────┼────────────────────┐                 │
 * │         │                    │                    │                 │
 * │         ▼                    ▼                    ▼                 │
 * │  ┌────────────┐      ┌────────────┐      ┌────────────┐            │
 * │  │   UMIM     │      │   UMI-OS   │      │   Renode   │            │
 * │  │  Backend   │      │  Backend   │      │  Backend   │            │
 * │  │            │      │            │      │            │            │
 * │  │ AudioWork- │      │ Full       │      │ WebSocket  │            │
 * │  │ let+WASM   │      │ Kernel Sim │      │ + Bridge   │            │
 * │  └────────────┘      └────────────┘      └────────────┘            │
 * └─────────────────────────────────────────────────────────────────────┘
 */

// Re-export everything from the new modular structure
export {
    // Types
    BackendInterface,
    BackendType,

    // Backends
    UmimBackend,
    UmimGenericBackend,
    WasmBackend,  // Legacy alias
    UmiosBackend,
    UmiosGenericBackend,
    RenodeBackend,
    CortexMBackend,

    // Manager
    BackendManager,
    backendManager,
} from './lib/umi_web/core/index.js';

// Import for window exports
import {
    BackendType,
    UmimBackend,
    UmimGenericBackend,
    WasmBackend,
    UmiosBackend,
    UmiosGenericBackend,
    RenodeBackend,
    BackendManager,
    backendManager,
} from './lib/umi_web/core/index.js';

// Export for use in HTML (backward compatibility)
if (typeof window !== 'undefined') {
    window.BackendManager = BackendManager;
    window.BackendType = BackendType;
    window.UmimBackend = UmimBackend;
    window.UmimGenericBackend = UmimGenericBackend;
    window.UmiosBackend = UmiosBackend;
    window.UmiosGenericBackend = UmiosGenericBackend;
    window.WasmBackend = WasmBackend;  // Legacy alias
    window.RenodeBackend = RenodeBackend;
    window.backendManager = backendManager;
}
