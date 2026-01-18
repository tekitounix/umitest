/**
 * UMI-OS Synth Simulator - AudioWorklet Processor
 *
 * Runs the WASM synth in the audio worklet thread for glitch-free audio.
 * Uses raw WASM instantiation (no Emscripten runtime in worklet).
 */

class SynthWorkletProcessor extends AudioWorkletProcessor {
    constructor(options) {
        super();

        this.wasmReady = false;
        this.wasmInstance = null;
        this.wasmMemory = null;
        this.fn = null;
        this.kernel = null;  // Kernel state functions
        this.sysex = null;   // SysEx IO functions
        this.shell = null;   // Shell functions
        this.malloc = null;  // Memory allocation function
        this.inputPtr = 0;   // Will be allocated via malloc
        this.outputPtr = 0;  // Will be allocated via malloc
        this.sysexBuffer = new Uint8Array(4096);
        this.shellInputPtr = 0;  // Will be allocated via malloc

        // Handle messages from main thread
        this.port.onmessage = (e) => this.handleMessage(e.data);
    }

    handleMessage(msg) {
        switch (msg.type) {
            case 'init':
                this.initWasm(msg.wasmBytes);
                break;
            case 'note-on':
                if (this.wasmReady && this.fn) {
                    console.log('[SynthWorklet] noteOn:', msg.note, msg.velocity);
                    this.fn.noteOn(msg.note, msg.velocity);
                }
                break;
            case 'note-off':
                if (this.wasmReady && this.fn) {
                    this.fn.noteOff(msg.note);
                }
                break;
            case 'midi':
                if (this.wasmReady && this.fn) {
                    this.fn.midi(msg.status, msg.data1, msg.data2);
                }
                break;
            case 'set-param':
                if (this.wasmReady && this.fn) {
                    this.fn.setParam(msg.id, msg.value);
                }
                break;
            case 'get-params':
                if (this.wasmReady && this.fn) {
                    this.sendParamInfo();
                }
                break;
            case 'get-kernel-state':
                if (this.wasmReady && this.kernel) {
                    this.sendKernelState();
                }
                break;
            case 'reset-dsp-peak':
                if (this.wasmReady && this.kernel && this.kernel.resetDspPeak) {
                    this.kernel.resetDspPeak();
                }
                break;
            case 'set-rtc':
                if (this.wasmReady && this.kernel && this.kernel.setRtc) {
                    const lo = msg.epoch & 0xFFFFFFFF;
                    const hi = Math.floor(msg.epoch / 0x100000000);
                    this.kernel.setRtc(lo, hi);
                }
                break;
            case 'reset':
                if (this.wasmReady && this.fn && this.fn.reset) {
                    this.fn.reset();
                }
                break;
            case 'shell-input':
                if (this.wasmReady && this.shell) {
                    this.handleShellInput(msg.text);
                }
                break;
            case 'get-sysex':
                if (this.wasmReady && this.sysex) {
                    this.sendSysExData();
                }
                break;
            case 'hw-settings':
                if (this.wasmReady && this.hw) {
                    this.applyHwSettings(msg.settings);
                }
                break;
            case 'get-irq-state':
                if (this.wasmReady && this.irq) {
                    this.sendIrqState();
                }
                break;
            case 'trigger-irq':
                if (this.wasmReady && this.irq && this.irq.trigger) {
                    this.irq.trigger(msg.flag);
                }
                break;
            case 'set-adc':
                if (this.wasmReady && this.hwState && this.hwState.setAdc) {
                    this.hwState.setAdc(msg.channel, msg.value);
                }
                break;
            case 'set-encoder':
                if (this.wasmReady && this.hwState && this.hwState.setEncoder) {
                    this.hwState.setEncoder(msg.encoder, msg.position);
                }
                break;
            case 'trigger-gpio':
                if (this.wasmReady && this.hwState && this.hwState.triggerGpio) {
                    this.hwState.triggerGpio(msg.pinMask);
                }
                break;
            case 'send-notify':
                if (this.wasmReady && this.notify && this.notify.send) {
                    this.notify.send(msg.task, msg.bits);
                }
                break;
        }
    }

    applyHwSettings(settings) {
        const hw = this.hw;
        if (!hw) return;

        // CPU timing parameters
        if (hw.setCpuFreq) hw.setCpuFreq(settings.cpuFreq || 168);
        if (hw.setIsrOverhead) hw.setIsrOverhead(settings.isrOverhead || 100);
        if (hw.setBaseCycles) hw.setBaseCycles(settings.baseCycles || 20);
        if (hw.setVoiceCycles) hw.setVoiceCycles(settings.voiceCycles || 200);

        // Memory configuration (basic)
        if (settings.sramTotalKb !== undefined && hw.setSramTotalKb) {
            hw.setSramTotalKb(settings.sramTotalKb);
        }
        if (settings.heapSizeKb !== undefined && hw.setHeapSizeKb) {
            hw.setHeapSizeKb(settings.heapSizeKb);
        }
        if (settings.taskStackBytes !== undefined && hw.setTaskStackBytes) {
            hw.setTaskStackBytes(settings.taskStackBytes);
        }
        if (settings.flashTotalKb !== undefined && hw.setFlashTotalKb) {
            hw.setFlashTotalKb(settings.flashTotalKb);
        }
        if (settings.flashUsedKb !== undefined && hw.setFlashUsedKb) {
            hw.setFlashUsedKb(settings.flashUsedKb);
        }

        // Memory configuration (detailed)
        if (settings.dataBssKb !== undefined && hw.setDataBssKb) {
            hw.setDataBssKb(settings.dataBssKb);
        }
        if (settings.mainStackKb !== undefined && hw.setMainStackKb) {
            hw.setMainStackKb(settings.mainStackKb);
        }
        if (settings.taskCount !== undefined && hw.setTaskCount) {
            hw.setTaskCount(settings.taskCount);
        }
        if (settings.dmaBufferKb !== undefined && hw.setDmaBufferKb) {
            hw.setDmaBufferKb(settings.dmaBufferKb);
        }
        if (settings.sharedMemKb !== undefined && hw.setSharedMemKb) {
            hw.setSharedMemKb(settings.sharedMemKb);
        }
        if (settings.ccmTotalKb !== undefined && hw.setCcmTotalKb) {
            hw.setCcmTotalKb(settings.ccmTotalKb);
        }
        if (settings.ccmUsedKb !== undefined && hw.setCcmUsedKb) {
            hw.setCcmUsedKb(settings.ccmUsedKb);
        }

        // Battery state
        if (this.kernel && this.kernel.setBattery) {
            this.kernel.setBattery(
                settings.batteryPercent || 100,
                settings.batteryCharging ? 1 : 0,
                settings.batteryVoltage || 4200
            );
        }

        // Watchdog
        if (this.kernel) {
            if (settings.wdtEnabled && settings.wdtTimeout > 0) {
                if (this.kernel.wdtInit) {
                    this.kernel.wdtInit(settings.wdtTimeout);
                }
            }
        }
    }

    handleShellInput(text) {
        if (!this.shell || !this.wasmMemory) return;

        // Write input string to WASM memory at shellInputPtr
        const encoder = new TextEncoder();
        const bytes = encoder.encode(text + '\n');
        const heap = new Uint8Array(this.wasmMemory.buffer);

        // Find a safe location in WASM memory (after output buffer)
        // Use a fixed address that's beyond our buffers
        const inputPtr = 0x20000;  // 128KB - safe area

        heap.set(bytes, inputPtr);

        // Send to shell
        if (this.shell.input) {
            this.shell.input(inputPtr, bytes.length);
        }

        // Check if command is ready and execute
        if (this.shell.hasCommand && this.shell.hasCommand() !== 0) {
            const resultPtr = this.shell.execute();
            const result = this.readCString(resultPtr);
            this.port.postMessage({ type: 'shell-output', text: result });
        }
    }

    sendSysExData() {
        if (!this.sysex || !this.sysex.available) return;

        const available = this.sysex.available();
        if (available === 0) return;

        // Read SysEx data from WASM
        const toRead = Math.min(available, this.sysexBuffer.length);
        const heap = new Uint8Array(this.wasmMemory.buffer);

        // Use a fixed location in WASM memory for reading SysEx data
        const tempPtr = 0x21000;  // 132KB - safe area for SysEx buffer
        const bytesRead = this.sysex.read(tempPtr, toRead);

        if (bytesRead > 0) {
            // Copy to our buffer
            const data = heap.slice(tempPtr, tempPtr + bytesRead);
            this.port.postMessage({ type: 'sysex-data', data: data });
        }
    }

    sendParamInfo() {
        const count = this.fn.paramCount();
        const params = [];
        for (let i = 0; i < count; i++) {
            // Read string from WASM memory
            const namePtr = this.fn.paramName(i);
            const name = this.readCString(namePtr);
            params.push({
                id: i,
                name: name,
                min: this.fn.paramMin(i),
                max: this.fn.paramMax(i),
                default: this.fn.paramDefault(i),
                value: this.fn.getParam(i),
            });
        }
        this.port.postMessage({ type: 'params', params, sampleRate: this.fn.sampleRate() });
    }

    sendKernelState() {
        const k = this.kernel;
        if (!k) return;

        // Combine 32-bit hi/lo pairs into 64-bit values
        const uptime = k.uptimeLo ? (k.uptimeHi() * 0x100000000 + k.uptimeLo()) : 0;
        const rtc = k.rtcLo ? (k.rtcHi() * 0x100000000 + k.rtcLo()) : 0;
        const idleTime = k.idleTimeLo ? (k.idleTimeHi() * 0x100000000 + k.idleTimeLo()) : 0;

        // Build task list
        const taskCount = k.taskCount ? k.taskCount() : 4;
        const tasks = [];
        for (let i = 0; i < taskCount; i++) {
            const namePtr = k.taskName ? k.taskName(i) : 0;
            const name = namePtr ? this.readCString(namePtr) : `Task${i}`;
            const runTime = k.taskRunTimeLo
                ? (k.taskRunTimeHi(i) * 0x100000000 + k.taskRunTimeLo(i))
                : 0;
            const waitReason = this.notify && this.notify.waitReason
                ? this.notify.waitReason(i) : 0;
            tasks.push({
                id: i,
                name: name,
                state: k.taskState ? k.taskState(i) : 0,
                runTime: runTime,
                runCount: k.taskRunCount ? k.taskRunCount(i) : 0,
                waitReason: waitReason,
            });
        }

        this.port.postMessage({
            type: 'kernel-state',
            // Time
            uptime: uptime,
            rtc: rtc,
            // Audio
            bufferCount: k.audioBuffers ? k.audioBuffers() : 0,
            dropCount: k.audioDrops ? k.audioDrops() : 0,
            dspLoad: k.dspLoad ? k.dspLoad() : 0,
            dspPeak: k.dspPeak ? k.dspPeak() : 0,
            audioRunning: k.audioRunning ? k.audioRunning() !== 0 : false,
            // MIDI
            midiRx: k.midiRx ? k.midiRx() : 0,
            midiTx: k.midiTx ? k.midiTx() : 0,
            // Power
            batteryPercent: k.batteryPercent ? k.batteryPercent() : 100,
            batteryCharging: k.batteryCharging ? k.batteryCharging() !== 0 : false,
            batteryVoltage: k.batteryVoltage ? k.batteryVoltage() : 4200,
            usbConnected: k.usbConnected ? k.usbConnected() !== 0 : true,
            // Watchdog
            wdtEnabled: k.wdtEnabled ? k.wdtEnabled() !== 0 : false,
            wdtTimeout: k.wdtTimeout ? k.wdtTimeout() : 0,
            wdtExpired: k.wdtExpired ? k.wdtExpired() !== 0 : false,
            // Tasks
            taskCount: taskCount,
            taskReady: k.taskReady ? k.taskReady() : 1,
            taskBlocked: k.taskBlocked ? k.taskBlocked() : 0,
            ctxSwitches: k.ctxSwitches ? k.ctxSwitches() : 0,
            currentTask: k.currentTask ? k.currentTask() : 0,
            tasks: tasks,
            idleTime: idleTime,
            cpuUtil: k.cpuUtil ? k.cpuUtil() : 0,
            // Memory
            heapUsed: k.heapUsed ? k.heapUsed() : 0,
            heapTotal: k.heapTotal ? k.heapTotal() : 65536,
            heapPeak: k.heapPeak ? k.heapPeak() : 0,
            stackUsed: k.stackUsed ? k.stackUsed() : 1024,
            stackTotal: k.stackTotal ? k.stackTotal() : 4096,
            sramTotal: k.sramTotal ? k.sramTotal() : 131072,  // 128KB
            flashTotal: k.flashTotal ? k.flashTotal() : 524288,  // 512KB
            flashUsed: k.flashUsed ? k.flashUsed() : 65536,  // 64KB
            // IRQ Statistics
            irqCount: k.irqCount ? k.irqCount() : 0,
            audioIrqCount: k.audioIrqCount ? k.audioIrqCount() : 0,
            timerIrqCount: k.timerIrqCount ? k.timerIrqCount() : 0,
            dmaIrqCount: k.dmaIrqCount ? k.dmaIrqCount() : 0,
            systickCount: k.systickCount ? k.systickCount() : 0,
            // Error Statistics
            hardfaultCount: k.hardfaultCount ? k.hardfaultCount() : 0,
            stackOverflowCount: k.stackOverflowCount ? k.stackOverflowCount() : 0,
            mallocFailCount: k.mallocFailCount ? k.mallocFailCount() : 0,
            wdtResetCount: k.wdtResetCount ? k.wdtResetCount() : 0,
            // Detailed memory info
            ...this.getMemoryInfo(),
        });
    }

    getMemoryInfo() {
        const hw = this.hw;
        if (!hw) return {};

        const sramTotalKb = hw.sramTotalKb ? hw.sramTotalKb() : 128;
        const sramTotal = sramTotalKb * 1024;

        return {
            // Total SRAM
            sramTotal: sramTotal,
            // Memory warning state
            memWarning: hw.memWarning ? hw.memWarning() : 0,
            memCollisionPossible: hw.memCollisionPossible ? hw.memCollisionPossible() !== 0 : false,
            // Free gap between heap and stack
            freeGap: hw.memFreeGap ? hw.memFreeGap() : 0,
            // Memory layout (sizes in bytes)
            memLayout: {
                dataBss: hw.memDataBssSize ? hw.memDataBssSize() : 0,
                heap: hw.memHeapSize ? hw.memHeapSize() : 0,
                free: hw.memSramFree ? hw.memSramFree() : 0,
                taskStacks: hw.memTaskStacksSize ? hw.memTaskStacksSize() : 0,
                mainStack: hw.memMainStackSize ? hw.memMainStackSize() : 0,
                dma: hw.memDmaSize ? hw.memDmaSize() : 0,
                shared: hw.memSharedSize ? hw.memSharedSize() : 0,
            },
            // Detailed memory info for details section
            memDetails: {
                dataBss: hw.dataBssKb ? hw.dataBssKb() * 1024 : 0,
                taskStacks: hw.taskStackBytes && hw.taskCount
                    ? hw.taskStackBytes() * hw.taskCount()
                    : 0,
                mainStack: hw.mainStackKb ? hw.mainStackKb() * 1024 : 0,
                dma: hw.dmaBufferKb ? hw.dmaBufferKb() * 1024 : 0,
                shared: hw.sharedMemKb ? hw.sharedMemKb() * 1024 : 0,
                ccmTotal: hw.ccmTotalKb ? hw.ccmTotalKb() * 1024 : 0,
                ccmUsed: hw.ccmUsedKb ? hw.ccmUsedKb() * 1024 : 0,
            },
        };
    }

    readCString(ptr) {
        if (!ptr) return '';
        const heap = new Uint8Array(this.wasmMemory.buffer);
        let end = ptr;
        while (heap[end] !== 0 && end < heap.length) end++;
        // Manual UTF-8 decode (TextDecoder not available in AudioWorklet)
        let str = '';
        for (let i = ptr; i < end; i++) {
            str += String.fromCharCode(heap[i]);
        }
        return str;
    }

    async initWasm(wasmBytes) {
        try {
            // ASM_CONSTS table for EM_ASM calls
            // Address 4612 => location.reload() (from synth_sim.js)
            const ASM_CONSTS = {
                4612: () => { /* location.reload() - no-op in worklet */ }
            };

            // Import object for WASM (Emscripten minified imports)
            // wasmImports = { b: _emscripten_asm_const_int, a: _emscripten_resize_heap }
            const importObject = {
                a: {
                    // a = _emscripten_resize_heap
                    a: (requestedSize) => {
                        // Return 0 to indicate failure (fixed memory, no growth needed)
                        return 0;
                    },
                    // b = _emscripten_asm_const_int (for EM_ASM calls)
                    b: (code, sigPtr, argbuf) => {
                        // Execute ASM_CONST function if available
                        if (ASM_CONSTS[code]) {
                            return ASM_CONSTS[code]();
                        }
                        return 0;
                    }
                }
            };

            // Instantiate WASM
            const result = await WebAssembly.instantiate(wasmBytes, importObject);
            this.wasmInstance = result.instance;
            const exports = this.wasmInstance.exports;

            // Debug: log all exports
            console.log('[SynthWorklet] WASM exports:', Object.keys(exports).sort().join(', '));

            // Get memory (minified name: "c" based on synth_sim.js)
            this.wasmMemory = exports.c || exports.memory;

            // Create export function mappings based on synth_sim.js assignWasmExports()
            // d=__wasm_call_ctors (init runtime), e=umi_sim_init, f=umi_sim_reset, etc.
            this.fn = {
                init: exports.e || exports._umi_sim_init,
                reset: exports.f || exports._umi_sim_reset,
                process: exports.g || exports._umi_sim_process,
                noteOn: exports.h || exports._umi_sim_note_on,
                noteOff: exports.i || exports._umi_sim_note_off,
                cc: exports.j || exports._umi_sim_cc,
                midi: exports.k || exports._umi_sim_midi,
                load: exports.l || exports._umi_sim_load,
                positionLo: exports.m || exports._umi_sim_position_lo,
                positionHi: exports.n || exports._umi_sim_position_hi,
                sampleRate: exports.o || exports._umi_sim_sample_rate,
                paramCount: exports.p || exports._umi_sim_param_count,
                setParam: exports.q || exports._umi_sim_set_param,
                getParam: exports.r || exports._umi_sim_get_param,
                paramName: exports.s || exports._umi_sim_param_name,
                paramMin: exports.t || exports._umi_sim_param_min,
                paramMax: exports.u || exports._umi_sim_param_max,
                paramDefault: exports.v || exports._umi_sim_param_default,
            };

            // Kernel state functions (may have different minified names)
            // We'll try to find them by scanning exports
            this.kernel = this.mapKernelExports(exports);

            // SysEx IO functions
            this.sysex = this.mapSysExExports(exports);

            // Shell functions
            this.shell = this.mapShellExports(exports);

            // HW simulation parameter functions
            this.hw = this.mapHwExports(exports);

            // IRQ flag functions
            this.irq = this.mapIrqExports(exports);

            // Task notification functions
            this.notify = this.mapNotifyExports(exports);

            // HW state (shared memory) functions
            this.hwState = this.mapHwStateExports(exports);

            // Shared memory functions
            this.shm = this.mapShmExports(exports);

            // Get malloc function (minified as 'zc' based on synth_sim.js)
            this.malloc = exports.zc || exports._malloc;
            console.log('[SynthWorklet] malloc function:', this.malloc ? 'found' : 'not found');

            // Allocate buffers using malloc for safe memory access
            const BUFFER_SIZE = 128 * 4;  // 128 frames * 4 bytes per float
            if (this.malloc) {
                this.inputPtr = this.malloc(BUFFER_SIZE);
                this.outputPtr = this.malloc(BUFFER_SIZE);
                this.shellInputPtr = this.malloc(256);  // Shell input buffer
                console.log('[SynthWorklet] Buffers allocated:',
                    'input=' + this.inputPtr,
                    'output=' + this.outputPtr,
                    'shell=' + this.shellInputPtr);
            } else {
                // Fallback to fixed addresses if malloc not available
                console.warn('[SynthWorklet] malloc not found, using fixed addresses');
                this.inputPtr = 65536;
                this.outputPtr = 65536 + BUFFER_SIZE;
                this.shellInputPtr = 65536 + BUFFER_SIZE * 2;
            }

            // Initialize Emscripten runtime first (d = __wasm_call_ctors)
            const wasmCallCtors = exports.d;
            if (wasmCallCtors) {
                wasmCallCtors();
                console.log('[SynthWorklet] Emscripten runtime initialized');
            }

            // Initialize synth
            if (this.fn.init) {
                this.fn.init();
                console.log('[SynthWorklet] Synth initialized');
            }

            this.wasmReady = true;
            this.port.postMessage({ type: 'ready' });

            // Send initial parameter info
            this.sendParamInfo();

        } catch (err) {
            console.error('[SynthWorklet] Init error:', err);
            this.port.postMessage({ type: 'error', message: err.message });
        }
    }

    mapKernelExports(exports) {
        // Based on synth_sim.js assignWasmExports() mapping
        // Updated for new memory/IRQ/error exports
        const kernelFns = {
            // Time
            uptimeLo: exports.z || exports._umi_kernel_uptime_lo,
            uptimeHi: exports.A || exports._umi_kernel_uptime_hi,
            rtcLo: exports.B || exports._umi_kernel_rtc_lo,
            rtcHi: exports.C || exports._umi_kernel_rtc_hi,
            setRtc: exports.D || exports._umi_kernel_set_rtc,
            // Audio
            audioBuffers: exports.E || exports._umi_kernel_audio_buffers,
            audioDrops: exports.F || exports._umi_kernel_audio_drops,
            dspLoad: exports.G || exports._umi_kernel_dsp_load,
            dspPeak: exports.H || exports._umi_kernel_dsp_peak,
            resetDspPeak: exports.I || exports._umi_kernel_reset_dsp_peak,
            audioRunning: exports.J || exports._umi_kernel_audio_running,
            // MIDI
            midiRx: exports.K || exports._umi_kernel_midi_rx,
            midiTx: exports.L || exports._umi_kernel_midi_tx,
            // Power
            batteryPercent: exports.M || exports._umi_kernel_battery_percent,
            batteryCharging: exports.N || exports._umi_kernel_battery_charging,
            usbConnected: exports.O || exports._umi_kernel_usb_connected,
            batteryVoltage: exports.P || exports._umi_kernel_battery_voltage,
            setBattery: exports.Q || exports._umi_kernel_set_battery,
            // Watchdog
            wdtEnabled: exports.R || exports._umi_kernel_watchdog_enabled,
            wdtTimeout: exports.S || exports._umi_kernel_watchdog_timeout,
            wdtExpired: exports.T || exports._umi_kernel_watchdog_expired,
            wdtInit: exports.U || exports._umi_kernel_watchdog_init,
            wdtFeed: exports.V || exports._umi_kernel_watchdog_feed,
            // Tasks (basic)
            taskCount: exports.W || exports._umi_kernel_task_count,
            taskReady: exports.X || exports._umi_kernel_task_ready,
            taskBlocked: exports.Y || exports._umi_kernel_task_blocked,
            ctxSwitches: exports.Z || exports._umi_kernel_context_switches,
            // Tasks (extended)
            currentTask: exports._ || exports._umi_kernel_current_task,
            taskName: exports.$ || exports._umi_kernel_task_name,
            taskState: exports.aa || exports._umi_kernel_task_state,
            taskRunTimeLo: exports.ba || exports._umi_kernel_task_run_time_lo,
            taskRunTimeHi: exports.ca || exports._umi_kernel_task_run_time_hi,
            taskRunCount: exports.da || exports._umi_kernel_task_run_count,
            idleTimeLo: exports.ea || exports._umi_kernel_idle_time_lo,
            idleTimeHi: exports.fa || exports._umi_kernel_idle_time_hi,
            cpuUtil: exports.ga || exports._umi_kernel_cpu_util,
            // Memory
            heapUsed: exports.ha || exports._umi_kernel_heap_used,
            heapTotal: exports.ia || exports._umi_kernel_heap_total,
            heapPeak: exports.ja || exports._umi_kernel_heap_peak,
            stackUsed: exports.ka || exports._umi_kernel_stack_used,
            stackTotal: exports.la || exports._umi_kernel_stack_total,
            sramTotal: exports.ma || exports._umi_kernel_sram_total,
            flashTotal: exports.na || exports._umi_kernel_flash_total,
            flashUsed: exports.oa || exports._umi_kernel_flash_used,
            // IRQ Statistics
            irqCount: exports.pa || exports._umi_kernel_irq_count,
            audioIrqCount: exports.qa || exports._umi_kernel_audio_irq_count,
            timerIrqCount: exports.ra || exports._umi_kernel_timer_irq_count,
            dmaIrqCount: exports.sa || exports._umi_kernel_dma_irq_count,
            systickCount: exports.ta || exports._umi_kernel_systick_count,
            // Error Statistics
            hardfaultCount: exports.ua || exports._umi_kernel_hardfault_count,
            stackOverflowCount: exports.va || exports._umi_kernel_stack_overflow_count,
            mallocFailCount: exports.wa || exports._umi_kernel_malloc_fail_count,
            wdtResetCount: exports.xa || exports._umi_kernel_watchdog_reset_count,
            // Log
            logLevel: exports.ya || exports._umi_kernel_log_level,
            setLogLevel: exports.za || exports._umi_kernel_set_log_level,
            logCount: exports.Aa || exports._umi_kernel_log_count,
            // System
            reset: exports.Ba || exports._umi_kernel_reset,
        };

        // Count valid mappings
        const validCount = Object.values(kernelFns).filter(f => f).length;
        console.log('[SynthWorklet] Mapped', validCount, 'kernel exports');

        return kernelFns;
    }

    mapSysExExports(exports) {
        // SysEx IO exports - updated minified names
        return {
            available: exports.Ca || exports._umi_sysex_available,
            read: exports.Da || exports._umi_sysex_read,
            clear: exports.Ea || exports._umi_sysex_clear,
            logWrite: exports.Fa || exports._umi_log_write,
        };
    }

    mapShellExports(exports) {
        // Shell exports - updated minified names
        return {
            input: exports.Ga || exports._umi_shell_input,
            hasCommand: exports.Ha || exports._umi_shell_has_command,
            getCommand: exports.Ia || exports._umi_shell_get_command,
            execute: exports.Ja || exports._umi_shell_execute,
        };
    }

    mapHwExports(exports) {
        // HW simulation parameter exports - based on synth_sim.js assignWasmExports()
        // _umi_hw_cpu_freq = _a, _umi_hw_set_cpu_freq = $a, etc.
        return {
            // CPU timing
            cpuFreq: exports._a || exports._umi_hw_cpu_freq,
            setCpuFreq: exports.$a || exports._umi_hw_set_cpu_freq,
            isrOverhead: exports.ab || exports._umi_hw_isr_overhead,
            setIsrOverhead: exports.bb || exports._umi_hw_set_isr_overhead,
            baseCycles: exports.cb || exports._umi_hw_base_cycles,
            setBaseCycles: exports.db || exports._umi_hw_set_base_cycles,
            voiceCycles: exports.eb || exports._umi_hw_voice_cycles,
            setVoiceCycles: exports.fb || exports._umi_hw_set_voice_cycles,
            eventCycles: exports.gb || exports._umi_hw_event_cycles,
            setEventCycles: exports.hb || exports._umi_hw_set_event_cycles,
            // Memory configuration (basic)
            sramTotalKb: exports.ib || exports._umi_hw_sram_total_kb,
            setSramTotalKb: exports.jb || exports._umi_hw_set_sram_total_kb,
            heapSizeKb: exports.kb || exports._umi_hw_heap_size_kb,
            setHeapSizeKb: exports.lb || exports._umi_hw_set_heap_size_kb,
            taskStackBytes: exports.mb || exports._umi_hw_task_stack_bytes,
            setTaskStackBytes: exports.nb || exports._umi_hw_set_task_stack_bytes,
            flashTotalKb: exports.ob || exports._umi_hw_flash_total_kb,
            setFlashTotalKb: exports.pb || exports._umi_hw_set_flash_total_kb,
            flashUsedKb: exports.qb || exports._umi_hw_flash_used_kb,
            setFlashUsedKb: exports.rb || exports._umi_hw_set_flash_used_kb,
            // Memory configuration (detailed)
            dataBssKb: exports._umi_hw_data_bss_kb,
            setDataBssKb: exports._umi_hw_set_data_bss_kb,
            mainStackKb: exports._umi_hw_main_stack_kb,
            setMainStackKb: exports._umi_hw_set_main_stack_kb,
            taskCount: exports._umi_hw_task_count,
            setTaskCount: exports._umi_hw_set_task_count,
            dmaBufferKb: exports._umi_hw_dma_buffer_kb,
            setDmaBufferKb: exports._umi_hw_set_dma_buffer_kb,
            sharedMemKb: exports._umi_hw_shared_mem_kb,
            setSharedMemKb: exports._umi_hw_set_shared_mem_kb,
            ccmTotalKb: exports._umi_hw_ccm_total_kb,
            setCcmTotalKb: exports._umi_hw_set_ccm_total_kb,
            ccmUsedKb: exports._umi_hw_ccm_used_kb,
            setCcmUsedKb: exports._umi_hw_set_ccm_used_kb,
            // Memory layout query
            memWarning: exports._umi_mem_warning,
            memCollisionPossible: exports._umi_mem_collision_possible,
            memSramUsed: exports._umi_mem_sram_used,
            memSramFree: exports._umi_mem_sram_free,
            memFreeGap: exports._umi_mem_free_gap,
            // Memory regions
            memDataBssBase: exports._umi_mem_data_bss_base,
            memDataBssSize: exports._umi_mem_data_bss_size,
            memHeapBase: exports._umi_mem_heap_base,
            memHeapSize: exports._umi_mem_heap_size,
            memHeapUsed: exports._umi_mem_heap_used,
            setMemHeapUsed: exports._umi_mem_set_heap_used,
            memHeapPeak: exports._umi_mem_heap_peak,
            memTaskStacksBase: exports._umi_mem_task_stacks_base,
            memTaskStacksSize: exports._umi_mem_task_stacks_size,
            memTaskStacksUsed: exports._umi_mem_task_stacks_used,
            setMemTaskStacksUsed: exports._umi_mem_set_task_stacks_used,
            memMainStackBase: exports._umi_mem_main_stack_base,
            memMainStackSize: exports._umi_mem_main_stack_size,
            memMainStackUsed: exports._umi_mem_main_stack_used,
            setMemMainStackUsed: exports._umi_mem_set_main_stack_used,
            memDmaBase: exports._umi_mem_dma_base,
            memDmaSize: exports._umi_mem_dma_size,
            memSharedBase: exports._umi_mem_shared_base,
            memSharedSize: exports._umi_mem_shared_size,
            // Validation
            memValid: exports._umi_hw_mem_valid,
            memAllocated: exports._umi_hw_mem_allocated,
        };
    }

    mapIrqExports(exports) {
        // IRQ flag exports - based on synth_sim.js assignWasmExports()
        // _umi_irq_flag_pending = Ka, _umi_irq_flag_count = La, _umi_irq_trigger = Ma
        return {
            pending: exports.Ka || exports._umi_irq_flag_pending,
            count: exports.La || exports._umi_irq_flag_count,
            trigger: exports.Ma || exports._umi_irq_trigger,
        };
    }

    mapNotifyExports(exports) {
        // Task notification exports - based on synth_sim.js assignWasmExports()
        // _umi_notify_pending = Na, _umi_notify_send = Oa, _umi_task_wait_reason = Pa
        return {
            pending: exports.Na || exports._umi_notify_pending,
            send: exports.Oa || exports._umi_notify_send,
            waitReason: exports.Pa || exports._umi_task_wait_reason,
        };
    }

    mapHwStateExports(exports) {
        // HW state (shared memory) exports - based on synth_sim.js assignWasmExports()
        // _umi_hw_state_sequence = Ta, _umi_hw_adc_value = Ua, etc.
        return {
            sequence: exports.Ta || exports._umi_hw_state_sequence,
            adcValue: exports.Ua || exports._umi_hw_adc_value,
            setAdc: exports.Va || exports._umi_hw_set_adc,
            encoderPos: exports.Wa || exports._umi_hw_encoder_pos,
            setEncoder: exports.Xa || exports._umi_hw_set_encoder,
            gpioInput: exports.Ya || exports._umi_hw_gpio_input,
            triggerGpio: exports.Za || exports._umi_hw_trigger_gpio,
        };
    }

    mapShmExports(exports) {
        // Shared memory exports - based on synth_sim.js assignWasmExports()
        // _umi_shm_allocate = Qa, _umi_shm_size = Ra, _umi_shm_pool_used = Sa
        return {
            allocate: exports.Qa || exports._umi_shm_allocate,
            size: exports.Ra || exports._umi_shm_size,
            poolUsed: exports.Sa || exports._umi_shm_pool_used,
        };
    }

    sendIrqState() {
        const irq = this.irq;
        const notify = this.notify;
        const shm = this.shm;
        const hwState = this.hwState;

        if (!irq) return;

        // IRQ flag names matching the enum
        const irqFlagNames = [
            'DmaHalfTransfer', 'DmaComplete', 'SysTick', 'Timer2', 'Timer3',
            'AdcComplete', 'GpioExti', 'Usart1Rx', 'Usart1Tx', 'UsbSof'
        ];

        const irqFlags = irqFlagNames.map((name, i) => ({
            name,
            pending: irq.pending ? irq.pending(i) !== 0 : false,
            count: irq.count ? irq.count(i) : 0,
        }));

        // Task notification state
        const taskNotify = [];
        for (let i = 0; i < 4; i++) {
            taskNotify.push({
                pending: notify.pending ? notify.pending(i) : 0,
                waitReason: notify.waitReason ? notify.waitReason(i) : 0,
            });
        }

        // HW state from shared memory
        const adcValues = [];
        for (let i = 0; i < 8; i++) {
            adcValues.push(hwState && hwState.adcValue ? hwState.adcValue(i) : 0);
        }

        const encoderPos = [];
        for (let i = 0; i < 4; i++) {
            encoderPos.push(hwState && hwState.encoderPos ? hwState.encoderPos(i) : 0);
        }

        this.port.postMessage({
            type: 'irq-state',
            irqFlags,
            taskNotify,
            hwState: {
                sequence: hwState && hwState.sequence ? hwState.sequence() : 0,
                gpioInput: hwState && hwState.gpioInput ? hwState.gpioInput() : 0,
                adcValues,
                encoderPos,
            },
            shm: {
                poolUsed: shm && shm.poolUsed ? shm.poolUsed() : 0,
            },
        });
    }

    process(inputs, outputs, parameters) {
        const output = outputs[0];
        if (!output || output.length === 0) return true;

        const channel = output[0];
        const frames = channel.length;

        if (!this.wasmReady || !this.wasmInstance) {
            channel.fill(0);
            return true;
        }

        // Check if buffers are allocated
        if (!this.inputPtr || !this.outputPtr) {
            channel.fill(0);
            return true;
        }

        try {
            // Process audio
            this.fn.process(
                this.inputPtr,
                this.outputPtr,
                frames,
                Math.round(sampleRate)
            );

            // Copy from WASM heap to output
            const heap = new Float32Array(this.wasmMemory.buffer);
            const start = this.outputPtr / 4;
            channel.set(heap.subarray(start, start + frames));

            // Debug: check if output has any non-zero values
            const maxVal = Math.max(...Array.from(channel).map(Math.abs));
            if (maxVal > 0.001 && !this._debugDoneWithSound) {
                console.log('[SynthWorklet] SOUND DETECTED:',
                    'maxOutput=' + maxVal.toFixed(6));
                this._debugDoneWithSound = true;
            }

        } catch (err) {
            if (!this._errorLogged) {
                console.error('[SynthWorklet] Process error:', err);
                this._errorLogged = true;
            }
            channel.fill(0);
        }

        return true;
    }
}

registerProcessor('synth-worklet-processor', SynthWorkletProcessor);
