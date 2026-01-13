/**
 * UMI-OS WASM Headless Test Runner
 * 
 * Runs WASM tests in Node.js using Emscripten's generated JS glue.
 * Usage: node test-headless.mjs
 */

import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);

async function runTests() {
  console.log('='.repeat(50));
  console.log('UMI-OS WASM Headless Test Runner');
  console.log('='.repeat(50));
  console.log('');
  
  let passed = 0;
  let failed = 0;
  
  function assert(condition, name) {
    if (condition) {
      console.log(`✓ ${name}`);
      passed++;
    } else {
      console.log(`✗ ${name}`);
      failed++;
    }
  }
  
  try {
    // Load Emscripten module through its JS glue
    console.log('Loading WASM module via Emscripten glue...');
    
    // Dynamic import of ES6 module
    const wasmPath = join(__dirname, '../.build/wasm/umi_test.js');
    const { default: createModule } = await import(wasmPath);
    
    // Initialize the module
    const wasm = await createModule();
    console.log('WASM module loaded!');
    console.log('');
    
    // Run type tests
    console.log('--- Type Tests ---');
    const typeResult = wasm._umi_test_types();
    assert(typeResult === 6, `Type tests passed: ${typeResult}/6`);
    console.log('');
    
    // Run DSP tests
    console.log('--- DSP Tests ---');
    const dspResult = wasm._umi_test_dsp();
    assert(dspResult === 6, `DSP tests passed: ${dspResult}/6`);
    console.log('');
    
    // Memory tests
    console.log('--- Memory Tests ---');
    const ptr = wasm._malloc(1024);
    assert(ptr > 0, 'malloc works');
    wasm._free(ptr);
    console.log('✓ free works (no crash)');
    passed++;
    console.log('');
    
  } catch (e) {
    console.error('ERROR:', e.message);
    if (e.stack) {
      console.error(e.stack.split('\n').slice(0, 5).join('\n'));
    }
    failed++;
  }
  
  // Summary
  console.log('='.repeat(50));
  const total = passed + failed;
  if (failed === 0 && passed > 0) {
    console.log(`All ${passed} tests passed! ✓`);
  } else if (total === 0) {
    console.log('No tests were run');
  } else {
    console.log(`${passed}/${total} tests passed, ${failed} failed`);
  }
  console.log('='.repeat(50));
  
  process.exit(failed > 0 ? 1 : 0);
}

runTests();
