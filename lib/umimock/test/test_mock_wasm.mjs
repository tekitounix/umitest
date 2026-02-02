/**
 * umimock WASM Test Runner
 * Tests umimock module in Node.js environment.
 * Usage: node lib/umimock/test/test_mock_wasm.mjs
 */

import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { existsSync } from 'fs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);

// Test utilities
let passed = 0;
let failed = 0;

function assert(condition, name) {
    if (condition) {
        console.log(`  \u2713 ${name}`);
        passed++;
    } else {
        console.log(`  \u2717 ${name}`);
        failed++;
    }
}

function assertApprox(actual, expected, tolerance, name) {
    const diff = Math.abs(actual - expected);
    assert(diff <= tolerance, `${name} (got ${actual.toFixed(4)}, expected ${expected.toFixed(4)})`);
}

// Load umimock WASM module
async function loadModule() {
    const modulePath = join(__dirname, '../../../build/wasm/wasm32/release/umimock_wasm.js');
    if (!existsSync(modulePath)) {
        console.log(`Module not found: ${modulePath}`);
        console.log('Build first: xmake build umimock_wasm');
        process.exit(1);
    }

    const { default: createModule } = await import(modulePath);
    return await createModule();
}

async function main() {
    console.log('\n[umimock WASM tests]');

    const wasm = await loadModule();
    assert(wasm !== null, 'Module loaded');

    // Constant signal
    console.log('\n[Constant signal]');
    assertApprox(wasm._umimock_constant(0.5), 0.5, 0.001, 'constant 0.5');
    assertApprox(wasm._umimock_constant(0.0), 0.0, 0.001, 'constant 0.0');
    assertApprox(wasm._umimock_constant(1.0), 1.0, 0.001, 'constant 1.0');

    // Ramp signal
    console.log('\n[Ramp signal]');
    assertApprox(wasm._umimock_ramp_first(), 0.01, 0.001, 'first ramp sample');

    // set_value / get_value roundtrip
    console.log('\n[set_value / get_value]');
    assertApprox(wasm._umimock_set_and_get(0.75), 0.75, 0.001, 'set_and_get 0.75');

    // Reset
    console.log('\n[reset]');
    assertApprox(wasm._umimock_reset_value(), 0.0, 0.001, 'reset returns default');

    // fill_buffer
    console.log('\n[fill_buffer]');
    assert(wasm._umimock_fill_buffer_check(0.25, 8) === 1, 'fill_buffer all samples match');

    // Summary
    console.log('\n=================================');
    if (failed === 0) {
        console.log(`Tests: ${passed}/${passed + failed} passed`);
    } else {
        console.log(`Tests: ${passed}/${passed + failed} passed, ${failed} FAILED`);
    }
    console.log('=================================\n');

    process.exit(failed > 0 ? 1 : 0);
}

main().catch(e => {
    console.error(e);
    process.exit(1);
});
