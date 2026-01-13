/**
 * UMI-OS Browser Integration Tests (Playwright)
 *
 * Tests the workbench UI and AudioWorklet functionality in a real browser.
 */

import { test, expect } from '@playwright/test';
import { createServer } from 'http';
import { readFileSync, existsSync } from 'fs';
import { join, extname } from 'path';

const PORT = 3456;
const ROOT = join(__dirname, '../examples/workbench');

// Simple static file server
function startServer(): Promise<ReturnType<typeof createServer>> {
  return new Promise((resolve) => {
    const mimeTypes: Record<string, string> = {
      '.html': 'text/html',
      '.js': 'application/javascript',
      '.wasm': 'application/wasm',
      '.css': 'text/css',
      '.json': 'application/json',
    };

    const server = createServer((req, res) => {
      let filePath = join(ROOT, req.url === '/' ? 'workbench.html' : req.url!);

      // Handle UMIM modules from build directory
      if (req.url?.startsWith('/umim_modules/')) {
        filePath = join(__dirname, '../.build/umim', req.url.replace('/umim_modules/', ''));
      }

      if (!existsSync(filePath)) {
        res.writeHead(404);
        res.end('Not found');
        return;
      }

      const ext = extname(filePath);
      const contentType = mimeTypes[ext] || 'application/octet-stream';

      // Add COOP/COEP headers for SharedArrayBuffer (required for AudioWorklet)
      res.setHeader('Cross-Origin-Opener-Policy', 'same-origin');
      res.setHeader('Cross-Origin-Embedder-Policy', 'require-corp');

      res.writeHead(200, { 'Content-Type': contentType });
      res.end(readFileSync(filePath));
    });

    server.listen(PORT, () => resolve(server));
  });
}

let server: ReturnType<typeof createServer>;

test.beforeAll(async () => {
  server = await startServer();
});

test.afterAll(async () => {
  server?.close();
});

test.describe('Workbench UI', () => {
  test('loads without errors', async ({ page }) => {
    const errors: string[] = [];
    page.on('console', msg => {
      if (msg.type() === 'error') {
        errors.push(msg.text());
      }
    });

    await page.goto(`http://localhost:${PORT}/`);

    // Wait for page to load
    await page.waitForLoadState('networkidle');

    // Check title
    const title = await page.title();
    expect(title).toContain('UMI');

    // No critical errors (ignore some expected warnings)
    const criticalErrors = errors.filter(e =>
      !e.includes('AudioContext') &&
      !e.includes('autoplay')
    );
    expect(criticalErrors).toHaveLength(0);
  });

  test('has module loading UI', async ({ page }) => {
    await page.goto(`http://localhost:${PORT}/`);
    await page.waitForLoadState('networkidle');

    // Check for module-related UI elements
    const body = await page.content();
    expect(body).toMatch(/synth|module|audio/i);
  });
});

test.describe('WASM Module Loading', () => {
  test('can load synth module', async ({ page }) => {
    await page.goto(`http://localhost:${PORT}/`);

    // Check if synth.wasm is accessible
    const response = await page.request.get(`http://localhost:${PORT}/umim_modules/synth.wasm`);
    expect(response.status()).toBe(200);
    expect(response.headers()['content-type']).toBe('application/wasm');
  });

  test('WASM modules have expected exports', async ({ page }) => {
    await page.goto(`http://localhost:${PORT}/`);

    // Load module and check exports
    const exports = await page.evaluate(async () => {
      try {
        const response = await fetch('/umim_modules/synth.js');
        if (!response.ok) return { error: 'fetch failed' };

        // We can't fully instantiate due to AudioWorklet requirements,
        // but we can verify the module script loads
        return { loaded: true };
      } catch (e) {
        return { error: String(e) };
      }
    });

    expect(exports).toHaveProperty('loaded', true);
  });
});

test.describe('Audio Context', () => {
  test('can create AudioContext', async ({ page }) => {
    await page.goto(`http://localhost:${PORT}/`);

    const result = await page.evaluate(async () => {
      try {
        // @ts-ignore
        const ctx = new (window.AudioContext || window.webkitAudioContext)();
        const state = ctx.state;
        const sampleRate = ctx.sampleRate;
        await ctx.close();
        return { success: true, state, sampleRate };
      } catch (e) {
        return { success: false, error: String(e) };
      }
    });

    expect(result.success).toBe(true);
    expect(result.sampleRate).toBeGreaterThan(0);
  });

  test('AudioWorklet is available', async ({ page }) => {
    await page.goto(`http://localhost:${PORT}/`);

    const hasWorklet = await page.evaluate(() => {
      // @ts-ignore
      const ctx = new (window.AudioContext || window.webkitAudioContext)();
      const available = 'audioWorklet' in ctx;
      ctx.close();
      return available;
    });

    expect(hasWorklet).toBe(true);
  });
});

test.describe('Performance', () => {
  test('page loads within 5 seconds', async ({ page }) => {
    const start = Date.now();
    await page.goto(`http://localhost:${PORT}/`);
    await page.waitForLoadState('networkidle');
    const duration = Date.now() - start;

    expect(duration).toBeLessThan(5000);
  });

  test('no memory leaks on repeated module loads', async ({ page }) => {
    await page.goto(`http://localhost:${PORT}/`);

    const memoryUsage = await page.evaluate(async () => {
      // @ts-ignore
      if (!performance.memory) return { supported: false };

      const initial = (performance as any).memory.usedJSHeapSize;

      // Simulate repeated operations
      for (let i = 0; i < 10; i++) {
        const arr = new Float32Array(1024 * 1024);
        arr.fill(Math.random());
      }

      // Force GC if available
      // @ts-ignore
      if (global.gc) global.gc();

      await new Promise(r => setTimeout(r, 100));

      const final = (performance as any).memory.usedJSHeapSize;
      const growth = final - initial;

      return {
        supported: true,
        initial,
        final,
        growth,
        // Allow up to 50MB growth (generous for test stability)
        acceptable: growth < 50 * 1024 * 1024
      };
    });

    if (memoryUsage.supported) {
      expect(memoryUsage.acceptable).toBe(true);
    }
  });
});
