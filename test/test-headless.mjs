/**
 * UMI-OS WASM Headless Test Runner
 *
 * Tests UMIM modules in Node.js environment.
 * Usage: node test-headless.mjs
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
    console.log(`  ✓ ${name}`);
    passed++;
  } else {
    console.log(`  ✗ ${name}`);
    failed++;
  }
}

function assertApprox(actual, expected, tolerance, name) {
  const diff = Math.abs(actual - expected);
  assert(diff <= tolerance, `${name} (got ${actual.toFixed(4)}, expected ${expected.toFixed(4)})`);
}

// Load a UMIM module
async function loadModule(name) {
  const modulePath = join(__dirname, `../.build/umim/${name}.js`);
  if (!existsSync(modulePath)) {
    console.log(`  ⚠ Module not found: ${modulePath}`);
    return null;
  }

  const { default: createModule } = await import(modulePath);
  const wasm = await createModule();

  // Ensure HEAPF32 is available
  if (!wasm.HEAPF32) {
    // Create view from memory buffer
    wasm.HEAPF32 = new Float32Array(wasm.wasmMemory?.buffer || wasm.HEAP8?.buffer);
  }

  return wasm;
}

// Test a single UMIM module
async function testModule(name, tests) {
  console.log(`\n[${name}]`);

  const wasm = await loadModule(name);
  if (!wasm) {
    failed++;
    return;
  }

  try {
    // Initialize
    wasm._umi_create(48000);
    assert(true, 'umi_create()');

    // Parameter introspection
    const paramCount = wasm._umi_get_param_count();
    assert(paramCount >= 0, `umi_get_param_count() = ${paramCount}`);

    // List parameters
    for (let i = 0; i < paramCount; i++) {
      const namePtr = wasm._umi_get_param_name(i);
      const paramName = wasm.UTF8ToString(namePtr);
      const min = wasm._umi_get_param_min(i);
      const max = wasm._umi_get_param_max(i);
      const def = wasm._umi_get_param_default(i);
      console.log(`    param[${i}]: ${paramName} (${min}-${max}, default=${def})`);
    }

    // Test parameter set/get roundtrip
    if (paramCount > 0) {
      const testValue = 0.5;
      wasm._umi_set_param(0, testValue);
      const gotValue = wasm._umi_get_param(0);
      assertApprox(gotValue, testValue, 0.001, 'set_param/get_param roundtrip');
    }

    // Test audio processing
    const bufferSize = 128;
    const inputPtr = wasm._malloc(bufferSize * 4);
    const outputPtr = wasm._malloc(bufferSize * 4);

    // Zero input
    for (let i = 0; i < bufferSize; i++) {
      wasm.HEAPF32[(inputPtr >> 2) + i] = 0.0;
    }

    // Process
    wasm._umi_process(inputPtr, outputPtr, bufferSize);
    assert(true, 'umi_process() completed');

    // Check output is valid (not NaN/Inf)
    let validOutput = true;
    for (let i = 0; i < bufferSize; i++) {
      const sample = wasm.HEAPF32[(outputPtr >> 2) + i];
      if (!isFinite(sample)) {
        validOutput = false;
        break;
      }
    }
    assert(validOutput, 'Output samples are finite');

    // Run module-specific tests
    if (tests) {
      await tests(wasm, { inputPtr, outputPtr, bufferSize });
    }

    wasm._free(inputPtr);
    wasm._free(outputPtr);

  } catch (e) {
    console.error(`  ERROR: ${e.message}`);
    failed++;
  }
}

// Module-specific test functions
const synthTests = async (wasm, { inputPtr, outputPtr, bufferSize }) => {
  // Test note on/off
  wasm._umi_note_on(60, 100);  // Middle C
  assert(true, 'umi_note_on(60, 100)');

  // Process a buffer
  wasm._umi_process(inputPtr, outputPtr, bufferSize);

  // Check that synth produces output when note is on
  let hasOutput = false;
  for (let i = 0; i < bufferSize; i++) {
    const sample = Math.abs(wasm.HEAPF32[(outputPtr >> 2) + i]);
    if (sample > 0.001) {
      hasOutput = true;
      break;
    }
  }
  assert(hasOutput, 'Synth produces audio when note is on');

  wasm._umi_note_off(60);
  assert(true, 'umi_note_off(60)');
};

const delayTests = async (wasm, { inputPtr, outputPtr, bufferSize }) => {
  // Feed impulse
  wasm.HEAPF32[(inputPtr >> 2)] = 1.0;
  for (let i = 1; i < bufferSize; i++) {
    wasm.HEAPF32[(inputPtr >> 2) + i] = 0.0;
  }

  // Process multiple buffers to let delay pass through
  for (let i = 0; i < 10; i++) {
    wasm._umi_process(inputPtr, outputPtr, bufferSize);
    // Clear input after first buffer
    if (i === 0) {
      for (let j = 0; j < bufferSize; j++) {
        wasm.HEAPF32[(inputPtr >> 2) + j] = 0.0;
      }
    }
  }
  assert(true, 'Delay processes multiple buffers');
};

const volumeTests = async (wasm, { inputPtr, outputPtr, bufferSize }) => {
  // Set volume to 0.5
  wasm._umi_set_param(0, 0.5);

  // Input 1.0
  for (let i = 0; i < bufferSize; i++) {
    wasm.HEAPF32[(inputPtr >> 2) + i] = 1.0;
  }

  wasm._umi_process(inputPtr, outputPtr, bufferSize);

  // Check output is approximately 0.5
  const sample = wasm.HEAPF32[(outputPtr >> 2)];
  assertApprox(sample, 0.5, 0.1, 'Volume scales input correctly');
};

// UMIM Patching Test - connect synth -> delay -> volume
async function testPatching() {
  console.log('\n[UMIM Patching Test]');

  const synth = await loadModule('synth');
  const delay = await loadModule('delay');
  const volume = await loadModule('volume');

  if (!synth || !delay || !volume) {
    console.log('  ⚠ Skipping patching test - modules not available');
    return;
  }

  try {
    const sampleRate = 48000;
    const bufferSize = 128;

    // Initialize all modules
    synth._umi_create(sampleRate);
    delay._umi_create(sampleRate);
    volume._umi_create(sampleRate);
    assert(true, 'All modules initialized');

    // Allocate buffers
    const buf1 = synth._malloc(bufferSize * 4);
    const buf2 = synth._malloc(bufferSize * 4);
    const buf3 = synth._malloc(bufferSize * 4);
    const buf4 = synth._malloc(bufferSize * 4);

    // Zero input
    for (let i = 0; i < bufferSize; i++) {
      synth.HEAPF32[(buf1 >> 2) + i] = 0.0;
    }

    // Trigger synth note
    synth._umi_note_on(60, 100);

    // Process chain: synth -> delay -> volume
    synth._umi_process(buf1, buf2, bufferSize);

    // Copy synth output to delay input
    for (let i = 0; i < bufferSize; i++) {
      delay.HEAPF32[(buf2 >> 2) + i] = synth.HEAPF32[(buf2 >> 2) + i];
    }
    delay._umi_process(buf2, buf3, bufferSize);

    // Copy delay output to volume input
    for (let i = 0; i < bufferSize; i++) {
      volume.HEAPF32[(buf3 >> 2) + i] = delay.HEAPF32[(buf3 >> 2) + i];
    }
    volume._umi_process(buf3, buf4, bufferSize);

    // Verify final output has signal
    let hasSignal = false;
    let maxSample = 0;
    for (let i = 0; i < bufferSize; i++) {
      const sample = Math.abs(volume.HEAPF32[(buf4 >> 2) + i]);
      maxSample = Math.max(maxSample, sample);
      if (sample > 0.001) hasSignal = true;
    }
    assert(hasSignal, `Patched chain produces output (max=${maxSample.toFixed(4)})`);

    // Cleanup
    synth._free(buf1);
    synth._free(buf2);
    synth._free(buf3);
    synth._free(buf4);
    synth._umi_note_off(60);

  } catch (e) {
    console.error(`  ERROR: ${e.message}`);
    failed++;
  }
}

// Main
async function main() {
  console.log('='.repeat(60));
  console.log('UMI-OS WASM Headless Test Runner');
  console.log('='.repeat(60));

  // Test individual modules
  await testModule('synth', synthTests);
  await testModule('delay', delayTests);
  await testModule('volume', volumeTests);

  // Test module patching
  await testPatching();

  // Summary
  console.log('\n' + '='.repeat(60));
  const total = passed + failed;
  if (failed === 0 && passed > 0) {
    console.log(`All ${passed} tests passed!`);
  } else if (total === 0) {
    console.log('No tests were run');
  } else {
    console.log(`${passed}/${total} tests passed, ${failed} failed`);
  }
  console.log('='.repeat(60));

  process.exit(failed > 0 ? 1 : 0);
}

main();
