// UMI-OS Canvas2D Backend - JavaScript Bridge
// 
// This file provides the JavaScript side of Canvas2DBackend.
// Include this in your HTML before loading the WASM module.
//
// Usage:
//   <canvas id="umi-canvas"></canvas>
//   <script src="umi-canvas.js"></script>
//   <script type="module">
//     import UmiSynth from './wasm/umi_synth.js';
//     const wasmModule = await UmiSynth();
//     UmiCanvas.init(document.getElementById('umi-canvas'), wasmModule);
//   </script>

const UmiCanvas = (function() {
    let canvas = null;
    let ctx = null;
    let width = 320;
    let height = 200;
    let wasmModule = null;
    
    // Convert RGBA uint32 to CSS color string
    function rgbaToCSS(rgba) {
        const r = (rgba >>> 24) & 0xFF;
        const g = (rgba >>> 16) & 0xFF;
        const b = (rgba >>> 8) & 0xFF;
        const a = (rgba & 0xFF) / 255;
        return `rgba(${r},${g},${b},${a})`;
    }
    
    return {
        // Initialize with canvas element and optional WASM module
        init(canvasElement, wasm = null) {
            canvas = canvasElement;
            ctx = canvas.getContext('2d');
            width = canvas.width;
            height = canvas.height;
            wasmModule = wasm;
            
            // Set font for text rendering
            ctx.font = '10px monospace';
            ctx.textBaseline = 'top';
        },
        
        // Get canvas element
        getCanvas() {
            return canvas;
        },
        
        // Get 2D context
        getContext() {
            return ctx;
        },
        
        // Set WASM module reference
        setWasmModule(wasm) {
            wasmModule = wasm;
        },
        
        // =====================================================
        // Drawing API (called from C++ via EM_JS)
        // =====================================================
        
        setSize(w, h) {
            if (canvas) {
                canvas.width = w;
                canvas.height = h;
                width = w;
                height = h;
                // Reset font after resize
                ctx.font = '10px monospace';
                ctx.textBaseline = 'top';
            }
        },
        
        beginFrame() {
            if (ctx) {
                ctx.clearRect(0, 0, width, height);
            }
        },
        
        endFrame() {
            // No-op for Canvas2D (drawing is immediate)
        },
        
        setPixel(x, y, rgba) {
            if (ctx) {
                ctx.fillStyle = rgbaToCSS(rgba);
                ctx.fillRect(x, y, 1, 1);
            }
        },
        
        fillRect(x, y, w, h, rgba) {
            if (ctx) {
                ctx.fillStyle = rgbaToCSS(rgba);
                ctx.fillRect(x, y, w, h);
            }
        },
        
        strokeRect(x, y, w, h, rgba) {
            if (ctx) {
                ctx.strokeStyle = rgbaToCSS(rgba);
                ctx.lineWidth = 1;
                ctx.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
            }
        },
        
        drawLine(x0, y0, x1, y1, rgba) {
            if (ctx) {
                ctx.strokeStyle = rgbaToCSS(rgba);
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.moveTo(x0 + 0.5, y0 + 0.5);
                ctx.lineTo(x1 + 0.5, y1 + 0.5);
                ctx.stroke();
            }
        },
        
        strokeCircle(cx, cy, r, rgba) {
            if (ctx) {
                ctx.strokeStyle = rgbaToCSS(rgba);
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.arc(cx, cy, r, 0, Math.PI * 2);
                ctx.stroke();
            }
        },
        
        fillCircle(cx, cy, r, rgba) {
            if (ctx) {
                ctx.fillStyle = rgbaToCSS(rgba);
                ctx.beginPath();
                ctx.arc(cx, cy, r, 0, Math.PI * 2);
                ctx.fill();
            }
        },
        
        strokeArc(cx, cy, r, startAngle, endAngle, rgba) {
            if (ctx) {
                ctx.strokeStyle = rgbaToCSS(rgba);
                ctx.lineWidth = 2;
                ctx.beginPath();
                // Canvas arc uses clockwise from 3 o'clock
                // Our angles are from 12 o'clock, so adjust
                const start = startAngle - Math.PI / 2;
                const end = endAngle - Math.PI / 2;
                ctx.arc(cx, cy, r, start, end);
                ctx.stroke();
            }
        },
        
        drawText(x, y, text, rgba) {
            if (ctx && text) {
                ctx.fillStyle = rgbaToCSS(rgba);
                ctx.fillText(text, x, y);
            }
        },
        
        textWidth(text) {
            if (ctx && text) {
                const metrics = ctx.measureText(text);
                return Math.ceil(metrics.width);
            }
            return 0;
        },
        
        // =====================================================
        // GUI Rendering (call from requestAnimationFrame)
        // =====================================================
        
        render() {
            if (wasmModule && wasmModule._umi_gui_render) {
                wasmModule._umi_gui_render();
            }
        },
        
        // =====================================================
        // Input Handling
        // =====================================================
        
        hitTest(x, y) {
            if (wasmModule && wasmModule._umi_gui_hit_test) {
                return wasmModule._umi_gui_hit_test(x, y);
            }
            return -1;
        },
        
        drag(paramIndex, deltaY) {
            if (wasmModule && wasmModule._umi_gui_drag) {
                wasmModule._umi_gui_drag(paramIndex, deltaY);
            }
        },
        
        click(paramIndex) {
            if (wasmModule && wasmModule._umi_gui_click) {
                wasmModule._umi_gui_click(paramIndex);
            }
        }
    };
})();

// Make globally available
if (typeof window !== 'undefined') {
    window.UmiCanvas = UmiCanvas;
}

// Export for module usage
if (typeof module !== 'undefined' && module.exports) {
    module.exports = UmiCanvas;
}
