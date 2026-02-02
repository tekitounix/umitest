// SPDX-License-Identifier: MIT
// =====================================================================
// UMI-OS Web Simulator (Full-Featured)
// =====================================================================
//
// Provides detailed umios kernel simulation for WASM environment.
// Includes task state tracking, DSP load monitoring, and shell interface.
//
// For a lightweight adapter without full kernel simulation, use web_adapter.hh
//
// Architecture:
//   ┌─────────────────────────────────────────┐
//   │ Application (synth_processor.hh)        │  ← Identical to embedded
//   └─────────────────────────────────────────┘
//             ↓
//   ┌─────────────────────────────────────────┐
//   │ WebSimAdapter (this file)               │  ← Full simulation
//   │ - Task state tracking                   │
//   │ - DSP load monitoring                   │
//   │ - Shell interface                       │
//   └─────────────────────────────────────────┘
//             ↓
//   ┌─────────────────────────────────────────┐
//   │ umi::Hw<WasmHwImpl>                     │  ← web_hal.hh
//   └─────────────────────────────────────────┘
//
// Usage:
//   #include <umim/web_sim.hh>
//   UMI_WEB_SIM_EXPORT_NAMED(MyProcessor, "Name", "Vendor", "1.0.0")
//   UMI_WEB_SIM_EXPORT_KERNEL()
//
// =====================================================================

#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <cstring>

// Include umios types directly
#include <umios/core/types.hh>
#include <umios/core/event.hh>
#include <umios/core/audio_context.hh>
#include <umios/kernel/umi_kernel.hh>
#include <umios/kernel/shell_commands.hh>

// Emscripten support
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define UMI_WEB_EXPORT EMSCRIPTEN_KEEPALIVE extern "C"
#else
#define UMI_WEB_EXPORT extern "C"
#endif

namespace umi::web {

// =====================================================================
// Task Simulation State
// =====================================================================
// Provides detailed task state tracking for monitoring/debugging.
// UMI-OS kernel structure:
//   Audio Runner   (highest) - I2S half-transfer IRQ → process() periodic
//   Control Runner (medium)  - 1ms timeslice, tickless, coroutine runtime
//   Driver Server  (medium)  - Driver request handling
//   System Monitor (lowest)  - watchdog, anomaly detection, WFI

enum class TaskId : uint8_t {
    IDLE = 0,        // System Monitor / Idle (lowest priority)
    DRIVER_SERVER,   // Driver Server (medium)
    CONTROL_RUNNER,  // Control Runner (medium)
    AUDIO_RUNNER,    // Audio Runner (highest priority)
    COUNT
};

enum class TaskState : uint8_t {
    READY = 0,
    RUNNING,
    BLOCKED,
    SUSPENDED
};

// =====================================================================
// Interrupt Flag System
// =====================================================================
// Hardware interrupts set flags which tasks poll or wait on.
// This models the actual ISR → notification → task wake pattern.

enum class IrqFlag : uint8_t {
    DMA_HALF_TRANSFER = 0,  // I2S DMA half-transfer complete
    DMA_COMPLETE,            // I2S DMA transfer complete
    SYS_TICK,                // SysTick timer (1ms)
    TIMER2,                  // General purpose timer 2
    TIMER3,                  // General purpose timer 3
    ADC_COMPLETE,            // ADC conversion complete
    GPIO_EXTI,               // External GPIO interrupt
    USART1_RX,               // UART receive
    USART1_TX,               // UART transmit complete
    USB_SOF,                 // USB Start of Frame
    COUNT
};

struct IrqFlagState {
    static constexpr uint8_t FLAG_COUNT = static_cast<uint8_t>(IrqFlag::COUNT);
    std::array<bool, FLAG_COUNT> pending{};      // ISR sets, task clears
    std::array<uint32_t, FLAG_COUNT> count{};    // Total IRQ counts per type

    void set(IrqFlag flag) {
        uint8_t idx = static_cast<uint8_t>(flag);
        pending[idx] = true;
        count[idx]++;
    }

    bool test_and_clear(IrqFlag flag) {
        uint8_t idx = static_cast<uint8_t>(flag);
        bool was_set = pending[idx];
        pending[idx] = false;
        return was_set;
    }

    bool test(IrqFlag flag) const {
        return pending[static_cast<uint8_t>(flag)];
    }

    uint32_t get_count(IrqFlag flag) const {
        return count[static_cast<uint8_t>(flag)];
    }

    void clear_all() {
        pending.fill(false);
    }
};

inline IrqFlagState g_irq_flags;

// =====================================================================
// Task Notification System
// =====================================================================
// Tasks can notify each other via notification values (32-bit bitmap).
// Similar to FreeRTOS xTaskNotify mechanism.

struct TaskNotification {
    static constexpr uint8_t TASK_COUNT = static_cast<uint8_t>(TaskId::COUNT);

    // Notification bits (can be used as event flags)
    static constexpr uint32_t NOTIFY_AUDIO_READY   = (1 << 0);
    static constexpr uint32_t NOTIFY_HW_UPDATE     = (1 << 1);
    static constexpr uint32_t NOTIFY_MIDI_RX       = (1 << 2);
    static constexpr uint32_t NOTIFY_SYSEX_RX      = (1 << 3);
    static constexpr uint32_t NOTIFY_SHELL_CMD     = (1 << 4);
    static constexpr uint32_t NOTIFY_TIMER         = (1 << 5);
    static constexpr uint32_t NOTIFY_USB_EVENT     = (1 << 6);
    static constexpr uint32_t NOTIFY_ERROR         = (1 << 7);

    std::array<uint32_t, TASK_COUNT> values{};    // Pending notification bits
    std::array<uint32_t, TASK_COUNT> wait_mask{}; // Bits task is waiting on

    void notify(TaskId task, uint32_t bits) {
        values[static_cast<uint8_t>(task)] |= bits;
    }

    uint32_t wait(TaskId task, uint32_t mask, bool clear = true) {
        uint8_t idx = static_cast<uint8_t>(task);
        wait_mask[idx] = mask;
        uint32_t result = values[idx] & mask;
        if (clear && result) {
            values[idx] &= ~result;
        }
        return result;
    }

    bool has_pending(TaskId task, uint32_t mask = 0xFFFFFFFF) const {
        return (values[static_cast<uint8_t>(task)] & mask) != 0;
    }

    void clear(TaskId task) {
        values[static_cast<uint8_t>(task)] = 0;
    }
};

inline TaskNotification g_task_notify;

// =====================================================================
// Shared Memory IPC
// =====================================================================
// Kernel and user tasks communicate via shared memory regions.
// Regions are allocated via system call and accessed through handles.

struct SharedMemRegion {
    void* base = nullptr;
    size_t size = 0;
    bool privileged_only = false;  // MPU can restrict access
    bool valid = false;
};

struct SharedMemoryManager {
    static constexpr size_t MAX_REGIONS = 8;
    static constexpr size_t REGION_ALIGN = 32;  // ARM MPU alignment

    // Pre-defined region IDs
    enum RegionId : uint8_t {
        REGION_HW_STATE = 0,      // Hardware state (read by ControlRunner)
        REGION_AUDIO_BUF = 1,     // DMA audio buffers
        REGION_MIDI_QUEUE = 2,    // MIDI event queue
        REGION_PARAM_TABLE = 3,   // Parameter values
        REGION_USER_0 = 4,        // User-defined regions
        REGION_USER_1 = 5,
        REGION_USER_2 = 6,
        REGION_USER_3 = 7,
    };

    std::array<SharedMemRegion, MAX_REGIONS> regions{};

    // Simulated memory pool (in real system, this is in SRAM)
    std::array<uint8_t, 8192> memory_pool{};
    size_t pool_used = 0;

    // Allocate a shared region (system call simulation)
    int allocate(RegionId id, size_t size, bool privileged = false) {
        if (id >= MAX_REGIONS) return -1;
        if (regions[id].valid) return -2;  // Already allocated

        // Align size
        size = (size + REGION_ALIGN - 1) & ~(REGION_ALIGN - 1);

        if (pool_used + size > memory_pool.size()) return -3;  // OOM

        regions[id].base = memory_pool.data() + pool_used;
        regions[id].size = size;
        regions[id].privileged_only = privileged;
        regions[id].valid = true;
        pool_used += size;

        return 0;
    }

    // Get region pointer (returns nullptr if invalid or access denied)
    void* get(RegionId id, bool privileged_access = false) {
        if (id >= MAX_REGIONS) return nullptr;
        auto& r = regions[id];
        if (!r.valid) return nullptr;
        if (r.privileged_only && !privileged_access) return nullptr;
        return r.base;
    }

    template<typename T>
    T* get_as(RegionId id, bool privileged_access = false) {
        return static_cast<T*>(get(id, privileged_access));
    }

    size_t get_size(RegionId id) const {
        if (id >= MAX_REGIONS || !regions[id].valid) return 0;
        return regions[id].size;
    }

    void reset() {
        for (auto& r : regions) {
            r = SharedMemRegion{};
        }
        pool_used = 0;
        memory_pool.fill(0);
    }
};

inline SharedMemoryManager g_shared_mem;

// =====================================================================
// Hardware State (in shared memory)
// =====================================================================
// DriverServer writes this, ControlRunner reads it.
// This represents the actual HW state structure in shared memory.

struct HwStateShared {
    // ADC values (12-bit, 0-4095)
    std::array<uint16_t, 8> adc_values{};

    // GPIO state
    uint32_t gpio_input = 0;
    uint32_t gpio_output = 0;

    // Encoder positions
    std::array<int16_t, 4> encoder_pos{};

    // Battery
    uint16_t battery_mv = 4200;
    uint8_t battery_percent = 100;
    uint8_t battery_flags = 0;  // bit0=charging, bit1=low_battery

    // Temperature (in 0.1°C)
    int16_t temp_mcu = 350;     // 35.0°C
    int16_t temp_board = 300;   // 30.0°C

    // Timestamp of last update
    uint32_t update_tick = 0;

    // Sequence number (for change detection)
    uint32_t sequence = 0;
};

struct TaskStats {
    TaskState state = TaskState::BLOCKED;
    uint64_t run_time_us = 0;       // Total CPU time consumed
    uint64_t last_run_us = 0;       // Last execution start time
    uint32_t run_count = 0;         // Number of times task ran
    uint32_t stack_high_water = 0;  // Stack high water mark
    uint16_t stack_size = 0;        // Allocated stack size
    uint8_t priority = 0;           // Task priority (0=lowest)
    const char* name = "";
    uint32_t wait_reason = 0;       // What notification bits this task is waiting for
};

// =====================================================================
// System Call Interface
// =====================================================================
// Simulates supervisor call (SVC) interface for user→kernel communication.
// In real system, these trap to kernel mode.

enum class SysCall : uint8_t {
    SHM_ALLOC = 0,       // Allocate shared memory region
    SHM_GET = 1,         // Get shared memory pointer
    SHM_FREE = 2,        // Free shared memory region
    NOTIFY_SEND = 10,    // Send notification to task
    NOTIFY_WAIT = 11,    // Wait for notification
    IRQ_ENABLE = 20,     // Enable IRQ
    IRQ_DISABLE = 21,    // Disable IRQ
    YIELD = 30,          // Yield CPU
    GET_TICK = 31,       // Get current tick count
};

struct SysCallResult {
    int32_t ret = 0;
    void* ptr = nullptr;
};

// System call handler (simulates kernel-mode execution)
inline SysCallResult syscall(SysCall call, uint32_t arg0 = 0, uint32_t arg1 = 0, uint32_t arg2 = 0) {
    SysCallResult result;

    switch (call) {
    case SysCall::SHM_ALLOC:
        result.ret = g_shared_mem.allocate(
            static_cast<SharedMemoryManager::RegionId>(arg0),
            arg1,
            arg2 != 0
        );
        break;

    case SysCall::SHM_GET:
        result.ptr = g_shared_mem.get(
            static_cast<SharedMemoryManager::RegionId>(arg0),
            arg1 != 0
        );
        result.ret = result.ptr ? 0 : -1;
        break;

    case SysCall::SHM_FREE:
        // In this simple implementation, we don't actually free
        result.ret = 0;
        break;

    case SysCall::NOTIFY_SEND:
        g_task_notify.notify(static_cast<TaskId>(arg0), arg1);
        result.ret = 0;
        break;

    case SysCall::NOTIFY_WAIT:
        result.ret = static_cast<int32_t>(
            g_task_notify.wait(static_cast<TaskId>(arg0), arg1, arg2 != 0)
        );
        break;

    default:
        result.ret = -1;  // Unknown syscall
        break;
    }

    return result;
}

// =====================================================================
// HW Simulation Parameters (configurable from JS)
// =====================================================================

// =====================================================================
// Memory Layout Model
// =====================================================================
// Simulates actual embedded SRAM memory layout:
//
// High Address ────────────────────────────
//   │  Main Stack (MSP)    ↓ grows down
//   │  ─────────────────────
//   │  Task Stacks (PSP)   ↓ grows down
//   │  ─────────────────────
//   │  (Free SRAM)
//   │  ─────────────────────
//   │  Heap                ↑ grows up
//   │  ─────────────────────
//   │  .bss (zero-init)
//   │  ─────────────────────
//   │  .data (init)
// Low Address ─────────────────────────────
//
// Stack overflow detection: if stack grows into heap, error
// Heap exhaustion: if malloc pushes into stack area, error

struct MemoryRegion {
    uint32_t base = 0;      // Start address (relative to SRAM base)
    uint32_t size = 0;      // Region size in bytes
    uint32_t used = 0;      // Current usage
    uint32_t peak = 0;      // Peak usage (high water mark)
    const char* name = "";
};

enum class MemoryWarning : uint8_t {
    NONE = 0,
    HEAP_LOW,           // Heap < 25% free
    STACK_LOW,          // Stack < 25% free
    HEAP_CRITICAL,      // Heap < 10% free
    STACK_CRITICAL,     // Stack < 10% free
    COLLISION,          // Stack and Heap regions would overlap
    OVERFLOW,           // Actual collision detected
};

struct MemoryLayout {
    // SRAM regions (addresses relative to SRAM base 0x20000000)
    MemoryRegion data_bss;      // .data + .bss (static variables)
    MemoryRegion heap;          // Dynamic allocation (grows up)
    MemoryRegion task_stacks;   // RTOS task stacks
    MemoryRegion main_stack;    // Main stack (MSP, grows down from top)

    // DMA buffers (must be in specific SRAM regions on some MCUs)
    MemoryRegion dma_buffers;   // Audio DMA double-buffer
    MemoryRegion shared_mem;    // IPC shared memory

    // Calculated values
    uint32_t sram_total = 0;
    uint32_t sram_used = 0;
    uint32_t sram_free = 0;
    uint32_t free_between_heap_stack = 0;  // Gap between heap top and stack bottom

    // Warning state
    MemoryWarning warning = MemoryWarning::NONE;
    bool collision_possible = false;

    // Check for potential collision
    void update_warning() {
        warning = MemoryWarning::NONE;
        collision_possible = false;

        // Calculate gap
        uint32_t heap_top = heap.base + heap.used;
        uint32_t stack_bottom = task_stacks.base;  // Lowest stack address

        if (heap_top >= stack_bottom) {
            warning = MemoryWarning::OVERFLOW;
            collision_possible = true;
            free_between_heap_stack = 0;
            return;
        }

        free_between_heap_stack = stack_bottom - heap_top;

        // Check if reserved heap + stack would collide
        uint32_t heap_max = heap.base + heap.size;
        uint32_t stack_max_down = task_stacks.base - task_stacks.size;
        if (heap_max > stack_max_down) {
            collision_possible = true;
        }

        // Calculate utilization warnings
        uint32_t heap_free_percent = (heap.size > 0) ?
            ((heap.size - heap.used) * 100 / heap.size) : 100;
        uint32_t stack_free_percent = (task_stacks.size > 0) ?
            ((task_stacks.size - task_stacks.used) * 100 / task_stacks.size) : 100;

        if (heap_free_percent < 10) {
            warning = MemoryWarning::HEAP_CRITICAL;
        } else if (stack_free_percent < 10) {
            warning = MemoryWarning::STACK_CRITICAL;
        } else if (heap_free_percent < 25) {
            warning = MemoryWarning::HEAP_LOW;
        } else if (stack_free_percent < 25) {
            warning = MemoryWarning::STACK_LOW;
        } else if (collision_possible) {
            warning = MemoryWarning::COLLISION;
        }
    }

    // Recalculate layout from HwSimParams
    void recalculate(uint32_t sram_kb, uint32_t data_bss_kb,
                     uint32_t heap_kb, uint32_t main_stack_kb,
                     uint32_t task_stack_bytes, uint32_t task_count,
                     uint32_t dma_buffer_kb, uint32_t shared_mem_kb) {
        sram_total = sram_kb * 1024;

        // Layout from bottom (low address) to top (high address):
        // [.data/.bss] [heap] ... free ... [task_stacks] [main_stack] [DMA] [shared]

        uint32_t addr = 0;

        // .data + .bss at bottom
        data_bss.base = addr;
        data_bss.size = data_bss_kb * 1024;
        data_bss.used = data_bss.size;  // Always fully used
        data_bss.name = ".data/.bss";
        addr += data_bss.size;

        // Heap grows up from after .data/.bss
        heap.base = addr;
        heap.size = heap_kb * 1024;
        heap.name = "Heap";
        // heap.used is set dynamically

        // Calculate remaining space for stacks
        uint32_t total_task_stack = task_stack_bytes * task_count;
        uint32_t total_main_stack = main_stack_kb * 1024;
        uint32_t dma_size = dma_buffer_kb * 1024;
        uint32_t shm_size = shared_mem_kb * 1024;

        // Layout from top (high address):
        // Shared memory at very top (for MPU alignment)
        uint32_t top = sram_total;
        shared_mem.size = shm_size;
        shared_mem.base = top - shm_size;
        shared_mem.name = "SharedMem";
        top = shared_mem.base;

        // DMA buffers (need to be aligned, often in specific SRAM bank)
        dma_buffers.size = dma_size;
        dma_buffers.base = top - dma_size;
        dma_buffers.used = dma_size;  // Always fully used when audio running
        dma_buffers.name = "DMA";
        top = dma_buffers.base;

        // Main stack (MSP) grows down from top
        main_stack.size = total_main_stack;
        main_stack.base = top - total_main_stack;
        main_stack.name = "MSP";
        top = main_stack.base;

        // Task stacks grow down
        task_stacks.size = total_task_stack;
        task_stacks.base = top - total_task_stack;
        task_stacks.name = "TaskStacks";

        // Calculate totals
        sram_used = data_bss.size + heap.used + task_stacks.used +
                    main_stack.used + dma_buffers.size + shared_mem.size;
        sram_free = sram_total - sram_used;

        update_warning();
    }
};

inline MemoryLayout g_memory_layout;

struct HwSimParams {
    // CPU timing (cycles per operation)
    uint32_t cpu_freq_mhz = 168;         // CPU clock frequency
    uint32_t isr_overhead_cycles = 100;  // ISR entry/exit overhead
    uint32_t base_cycles_per_sample = 20;  // Base cost per sample (buffer copy, etc.)
    uint32_t voice_cycles_per_sample = 200; // Voice processing cycles/sample
    uint32_t event_cycles = 80;           // MIDI event processing cycles

    // Memory configuration (detailed)
    uint32_t sram_total_kb = 128;        // Total SRAM (KB) - e.g., STM32F4=128KB
    uint32_t data_bss_kb = 8;            // .data + .bss size (KB)
    uint32_t heap_size_kb = 48;          // Heap size (KB)
    uint32_t main_stack_kb = 2;          // Main stack size (KB)
    uint32_t task_stack_bytes = 1024;    // Per-task stack size (bytes)
    uint32_t task_count = 4;             // Number of RTOS tasks
    uint32_t dma_buffer_kb = 2;          // DMA double-buffer size (KB)
    uint32_t shared_mem_kb = 8;          // Shared memory pool (KB)

    // Flash configuration
    uint32_t flash_total_kb = 512;       // Total Flash (KB)
    uint32_t flash_used_kb = 64;         // Used Flash (KB) - .text + .rodata

    // CCM RAM (Cortex-M4 specific, separate from main SRAM)
    uint32_t ccm_total_kb = 64;          // CCM RAM size (KB), 0 if not present
    uint32_t ccm_used_kb = 0;            // CCM usage (KB)

    // Convert cycles to microseconds
    float cycles_to_us(uint32_t cycles) const {
        return static_cast<float>(cycles) / static_cast<float>(cpu_freq_mhz);
    }

    // Get total heap in bytes
    uint32_t heap_total_bytes() const {
        return heap_size_kb * 1024;
    }

    // Get total stack in bytes (main stack only)
    uint32_t stack_total_bytes() const {
        return main_stack_kb * 1024;
    }

    // Get total task stacks in bytes
    uint32_t task_stacks_total_bytes() const {
        return task_stack_bytes * task_count;
    }

    // Get total allocated SRAM
    uint32_t sram_allocated_bytes() const {
        return (data_bss_kb + heap_size_kb + main_stack_kb + dma_buffer_kb + shared_mem_kb) * 1024
               + task_stack_bytes * task_count;
    }

    // Check if memory configuration is valid
    bool is_valid() const {
        return sram_allocated_bytes() <= sram_total_kb * 1024;
    }

    // Get free SRAM (gap between heap and stacks)
    uint32_t sram_free_bytes() const {
        uint32_t allocated = sram_allocated_bytes();
        return (allocated < sram_total_kb * 1024) ?
               (sram_total_kb * 1024 - allocated) : 0;
    }

    // Recalculate memory layout
    void recalculate_layout() {
        g_memory_layout.recalculate(
            sram_total_kb, data_bss_kb, heap_size_kb, main_stack_kb,
            task_stack_bytes, task_count, dma_buffer_kb, shared_mem_kb);
    }
};

inline HwSimParams g_hw_sim_params;

// =====================================================================
// Kernel State (for detailed monitoring)
// =====================================================================

struct KernelState {
    // --- Time ---
    uint64_t uptime_us = 0;
    uint64_t rtc_epoch_sec = 0;  // Unix timestamp (set from JS Date.now())

    // --- Audio ---
    uint32_t audio_buffer_count = 0;
    uint32_t audio_drop_count = 0;
    uint32_t dsp_load_percent_x100 = 0;  // 0-10000 = 0.00%-100.00%
    uint32_t dsp_load_peak_x100 = 0;
    bool audio_running = false;

    // --- MIDI ---
    uint32_t midi_rx_count = 0;
    uint32_t midi_tx_count = 0;

    // --- Power (simulated) ---
    uint8_t battery_percent = 100;
    bool battery_charging = false;
    bool usb_connected = true;  // Simulated as always connected in browser
    uint16_t battery_voltage_mv = 4200;  // 4.2V = full

    // --- Watchdog ---
    uint32_t watchdog_timeout_ms = 0;
    uint64_t watchdog_last_feed_us = 0;
    bool watchdog_enabled = false;

    // --- IRQ Statistics ---
    uint32_t irq_count = 0;           // Total IRQ count
    uint32_t audio_irq_count = 0;     // I2S half-transfer IRQ count
    uint32_t timer_irq_count = 0;     // Timer IRQ count
    uint32_t dma_irq_count = 0;       // DMA complete IRQ count
    uint32_t systick_count = 0;       // SysTick count (1ms)

    // --- Error Statistics ---
    uint32_t hardfault_count = 0;     // HardFault count
    uint32_t stack_overflow_count = 0; // Stack overflow detections
    uint32_t malloc_fail_count = 0;   // malloc() failure count
    uint32_t watchdog_reset_count = 0; // Watchdog reset count

    // --- Tasks ---
    static constexpr uint8_t MAX_TASKS = static_cast<uint8_t>(TaskId::COUNT);
    TaskStats tasks[MAX_TASKS] = {
        { TaskState::READY, 0, 0, 0, 128, 512, 0, "Idle" },           // Idle/Monitor
        { TaskState::READY, 0, 0, 0, 256, 1024, 1, "DriverSrv" },     // Driver Server
        { TaskState::READY, 0, 0, 0, 512, 2048, 2, "CtrlRunner" },    // Control Runner
        { TaskState::READY, 0, 0, 0, 256, 1024, 3, "AudioRunner" },   // Audio Runner
    };
    TaskId current_task = TaskId::IDLE;
    uint32_t context_switches = 0;
    uint64_t idle_time_us = 0;      // Time spent in idle (WFI)
    uint64_t total_run_time_us = 0; // Total non-idle time

    // --- Memory (simulated) ---
    uint32_t heap_used = 0;
    uint32_t heap_total = 65536;  // Updated from HwSimParams
    uint32_t heap_peak = 0;
    uint32_t sram_total = 131072; // Updated from HwSimParams (128KB)
    uint32_t flash_total = 524288; // Updated from HwSimParams (512KB)
    uint32_t flash_used = 65536;   // Updated from HwSimParams

    // --- Logging ---
    uint8_t log_level = 2;  // Info
    uint32_t log_count = 0;

    // Update memory sizes from HwSimParams
    void update_from_hw_params() {
        heap_total = g_hw_sim_params.heap_total_bytes();
        sram_total = g_hw_sim_params.sram_total_kb * 1024;
        flash_total = g_hw_sim_params.flash_total_kb * 1024;
        flash_used = g_hw_sim_params.flash_used_kb * 1024;
        // Update task stack sizes
        for (uint8_t i = 0; i < MAX_TASKS; ++i) {
            tasks[i].stack_size = static_cast<uint16_t>(g_hw_sim_params.task_stack_bytes);
        }
        // Recalculate memory layout
        g_hw_sim_params.recalculate_layout();
    }

    // Helper methods
    uint8_t task_count() const { return MAX_TASKS; }
    uint8_t task_ready_count() const {
        uint8_t count = 0;
        for (uint8_t i = 0; i < MAX_TASKS; ++i) {
            if (tasks[i].state == TaskState::READY || tasks[i].state == TaskState::RUNNING) {
                ++count;
            }
        }
        return count;
    }
    uint8_t task_blocked_count() const {
        uint8_t count = 0;
        for (uint8_t i = 0; i < MAX_TASKS; ++i) {
            if (tasks[i].state == TaskState::BLOCKED) {
                ++count;
            }
        }
        return count;
    }
    uint32_t total_stack_size() const {
        uint32_t total = 0;
        for (uint8_t i = 0; i < MAX_TASKS; ++i) {
            total += tasks[i].stack_size;
        }
        return total;
    }
    uint32_t total_stack_used() const {
        uint32_t total = 0;
        for (uint8_t i = 0; i < MAX_TASKS; ++i) {
            total += tasks[i].stack_high_water;
        }
        return total;
    }
};

// Global kernel state
inline KernelState g_kernel_state;

// ============================================================================
// SysEx Standard IO Buffer
// ============================================================================
// Circular buffer for stdout/stderr output via SysEx

struct SysExLogBuffer {
    static constexpr size_t BUFFER_SIZE = 4096;
    static constexpr size_t MAX_MESSAGE = 256;

    uint8_t buffer[BUFFER_SIZE];
    size_t write_pos = 0;
    size_t read_pos = 0;
    size_t count = 0;

    // UMI SysEx protocol constants
    static constexpr uint8_t SYSEX_START = 0xF0;
    static constexpr uint8_t SYSEX_END = 0xF7;
    static constexpr uint8_t UMI_ID[3] = {0x7E, 0x7F, 0x00};
    static constexpr uint8_t CMD_STDOUT = 0x01;
    static constexpr uint8_t CMD_STDERR = 0x02;

    void write(uint8_t cmd, const char* data, size_t len) {
        if (len == 0 || len > MAX_MESSAGE) return;

        // Calculate message size: F0 + ID(3) + CMD + SEQ + 7bit-data + CHECKSUM + F7
        size_t enc_len = (len * 8 + 6) / 7;  // 7-bit encoding
        size_t msg_len = 1 + 3 + 1 + 1 + enc_len + 1 + 1;
        if (msg_len > BUFFER_SIZE - count) return;  // Buffer full

        // Build message header
        size_t pos = write_pos;
        auto put = [&](uint8_t b) {
            buffer[pos] = b;
            pos = (pos + 1) % BUFFER_SIZE;
        };

        put(SYSEX_START);
        put(UMI_ID[0]); put(UMI_ID[1]); put(UMI_ID[2]);
        put(cmd);
        put(g_kernel_state.log_count & 0x7F);

        // 7-bit encode data
        uint8_t checksum = cmd ^ (g_kernel_state.log_count & 0x7F);
        const uint8_t* src = reinterpret_cast<const uint8_t*>(data);
        for (size_t i = 0; i < len; ) {
            // Process 7 bytes at a time into 8 output bytes
            uint8_t chunk[8] = {0};
            size_t n = (len - i > 7) ? 7 : (len - i);

            // High bits byte
            for (size_t j = 0; j < n; j++) {
                chunk[0] |= ((src[i + j] >> 7) & 1) << j;
            }
            // Low 7 bits
            for (size_t j = 0; j < n; j++) {
                chunk[1 + j] = src[i + j] & 0x7F;
            }

            // Output encoded bytes
            for (size_t j = 0; j <= n; j++) {
                put(chunk[j]);
                checksum ^= chunk[j];
            }
            i += n;
        }

        put(checksum & 0x7F);
        put(SYSEX_END);

        write_pos = pos;
        count += msg_len;
        g_kernel_state.log_count++;
    }

    void write_stdout(const char* data, size_t len) {
        write(CMD_STDOUT, data, len);
    }

    void write_stderr(const char* data, size_t len) {
        write(CMD_STDERR, data, len);
    }

    void write_stdout(const char* str) {
        write_stdout(str, strlen(str));
    }

    void write_stderr(const char* str) {
        write_stderr(str, strlen(str));
    }

    // Read pending data (returns number of bytes read)
    size_t read(uint8_t* out, size_t max_len) {
        size_t to_read = (count < max_len) ? count : max_len;
        for (size_t i = 0; i < to_read; i++) {
            out[i] = buffer[read_pos];
            read_pos = (read_pos + 1) % BUFFER_SIZE;
        }
        count -= to_read;
        return to_read;
    }

    size_t available() const { return count; }
    void clear() { write_pos = read_pos = count = 0; }
};

inline SysExLogBuffer g_sysex_log;

// ============================================================================
// Shell Configuration State (uses umi::os types from shell_commands.hh)
// ============================================================================

inline umi::os::ShellConfig g_shell_config = {
    .midi_channel = 1,
    .midi_transpose = 0,
    .audio_gain = 80,
    .sample_rate = 48000,
    .sleep_timeout_min = 5,
    .low_battery_threshold = 15,
    .shutdown_threshold = 5,
    .midi_monitor_enabled = false,
    .platform_name = "WASM Simulator",
    .serial_number = "SIM-00000000",
    .manufacture_date = 20240101,
    .factory_locked = false,
};

// ============================================================================
// Error Log and System Mode (uses umi::os types from shell_commands.hh)
// ============================================================================

inline umi::os::ErrorLog<16> g_error_log;
inline umi::os::SystemMode g_system_mode = umi::os::SystemMode::NORMAL;

// ============================================================================
// Shell Input Buffer (thin wrapper for WASM exports)
// ============================================================================
// Command processing is delegated to umi::os::ShellCommands via g_new_shell.
// This struct only handles character input buffering for the WASM API.

struct ShellInputBuffer {
    static constexpr size_t CMD_BUF_SIZE = 256;

    char cmd_buffer[CMD_BUF_SIZE]{};
    size_t cmd_len = 0;
    bool cmd_ready = false;

    void receive_char(char c) {
        if (c == '\n' || c == '\r') {
            if (cmd_len > 0) {
                cmd_buffer[cmd_len] = '\0';
                cmd_ready = true;
            }
        } else if (c == '\b' || c == 127) {  // Backspace
            if (cmd_len > 0) cmd_len--;
        } else if (cmd_len < CMD_BUF_SIZE - 1) {
            cmd_buffer[cmd_len++] = c;
        }
    }

    const char* get_command() {
        if (!cmd_ready) return nullptr;
        return cmd_buffer;
    }

    void clear_command() {
        cmd_len = 0;
        cmd_ready = false;
    }
};


inline ShellInputBuffer g_shell_input;

// ============================================================================
// State Provider for new ShellCommands system
// ============================================================================

/// Adapter between global state and ShellCommands template
struct WebSimStateProvider {
    /// Get kernel state view (maps from g_kernel_state)
    umi::os::KernelStateView& state() {
        // Update cached view from global state
        cached_state_.uptime_us = g_kernel_state.uptime_us;
        cached_state_.rtc_epoch_sec = g_kernel_state.rtc_epoch_sec;
        cached_state_.audio_buffer_count = g_kernel_state.audio_buffer_count;
        cached_state_.audio_drop_count = g_kernel_state.audio_drop_count;
        cached_state_.dsp_load_percent_x100 = g_kernel_state.dsp_load_percent_x100;
        cached_state_.dsp_load_peak_x100 = g_kernel_state.dsp_load_peak_x100;
        cached_state_.audio_running = g_kernel_state.audio_running;
        cached_state_.midi_rx_count = g_kernel_state.midi_rx_count;
        cached_state_.midi_tx_count = g_kernel_state.midi_tx_count;
        cached_state_.battery_percent = g_kernel_state.battery_percent;
        cached_state_.battery_charging = g_kernel_state.battery_charging;
        cached_state_.usb_connected = g_kernel_state.usb_connected;
        cached_state_.battery_voltage_mv = g_kernel_state.battery_voltage_mv;
        cached_state_.watchdog_timeout_ms = g_kernel_state.watchdog_timeout_ms;
        cached_state_.watchdog_last_feed_us = g_kernel_state.watchdog_last_feed_us;
        cached_state_.watchdog_enabled = g_kernel_state.watchdog_enabled;
        cached_state_.irq_count = g_kernel_state.irq_count;
        cached_state_.context_switches = g_kernel_state.context_switches;
        cached_state_.hardfault_count = g_kernel_state.hardfault_count;
        cached_state_.stack_overflow_count = g_kernel_state.stack_overflow_count;
        cached_state_.heap_used = g_kernel_state.heap_used;
        cached_state_.heap_total = g_kernel_state.heap_total;
        cached_state_.heap_peak = g_kernel_state.heap_peak;
        cached_state_.sram_total = g_kernel_state.sram_total;
        cached_state_.flash_total = g_kernel_state.flash_total;
        cached_state_.flash_used = g_kernel_state.flash_used;
        cached_state_.task_count = g_kernel_state.task_count();
        cached_state_.task_ready = g_kernel_state.task_ready_count();
        cached_state_.task_blocked = g_kernel_state.task_blocked_count();
        cached_state_.log_level = g_kernel_state.log_level;
        return cached_state_;
    }

    /// Get shell config (direct reference to global)
    umi::os::ShellConfig& config() {
        return g_shell_config;
    }

    /// Get error log
    umi::os::ErrorLog<16>& error_log() { return g_error_log; }

    /// Get system mode (direct reference to global)
    umi::os::SystemMode& system_mode() {
        return g_system_mode;
    }

    /// Reset system
    void reset_system() {
        // Will be handled by caller checking for "RESET_REQUESTED"
    }

    /// Feed watchdog
    void feed_watchdog() {
        g_kernel_state.watchdog_last_feed_us = g_kernel_state.uptime_us;
    }

    /// Enable/disable watchdog
    void enable_watchdog(bool enable) {
        g_kernel_state.watchdog_enabled = enable;
        if (enable) {
            g_kernel_state.watchdog_timeout_ms = 5000;
            g_kernel_state.watchdog_last_feed_us = g_kernel_state.uptime_us;
        }
    }

private:
    umi::os::KernelStateView cached_state_;
};

inline WebSimStateProvider g_state_provider;
inline umi::os::ShellCommands<WebSimStateProvider> g_new_shell{g_state_provider};

struct WebHwImpl {
    // --- Internal state ---
    static inline uint64_t current_time_us_ = 0;
    static inline uint64_t timer_target_us_ = UINT64_MAX;
    static inline bool in_critical_ = false;
    static inline bool audio_muted_ = false;
    static inline uint64_t process_start_us_ = 0;

    // --- Timer ---
    static void set_timer_absolute(uint64_t target_us) {
        timer_target_us_ = target_us;
    }

    static uint64_t monotonic_time_usecs() {
        return current_time_us_;
    }

    // Advance simulated time (called from process loop)
    static void advance_time(uint64_t delta_us) {
        current_time_us_ += delta_us;
    }

    // --- Critical Section (no-op in single-threaded WASM) ---
    static void enter_critical() { in_critical_ = true; }
    static void exit_critical() { in_critical_ = false; }

    // --- Multi-Core (no-op) ---
    static void trigger_ipi(uint8_t) {}
    static uint8_t current_core() { return 0; }

    // --- Context Switch (no-op) ---
    static void request_context_switch() {}

    // --- FPU (no-op) ---
    static void save_fpu() {}
    static void restore_fpu() {}

    // --- Audio ---
    static void mute_audio_dma() { audio_muted_ = true; }
    static void unmute_audio_dma() { audio_muted_ = false; }
    static bool is_muted() { return audio_muted_; }

    // --- Persistent Storage (no-op, could use localStorage) ---
    static void write_backup_ram(const void*, size_t) {}
    static void read_backup_ram(void*, size_t) {}

    // --- MPU (no-op) ---
    static void configure_mpu_region(size_t, const void*, size_t, bool, bool) {}

    // --- Cache (no-op) ---
    static void cache_clean(const void*, size_t) {}
    static void cache_invalidate(void*, size_t) {}
    static void cache_clean_invalidate(void*, size_t) {}

    // --- System ---
    static void system_reset() {
#ifdef __EMSCRIPTEN__
        EM_ASM({ location.reload(); });
#endif
    }
    static void enter_sleep() {}
    [[noreturn]] static void start_first_task() { while(true) {} }

    // --- Watchdog ---
    static void watchdog_init(uint32_t timeout_ms) {
        g_kernel_state.watchdog_timeout_ms = timeout_ms;
        g_kernel_state.watchdog_enabled = (timeout_ms > 0);
        g_kernel_state.watchdog_last_feed_us = current_time_us_;
    }
    static void watchdog_feed() {
        g_kernel_state.watchdog_last_feed_us = current_time_us_;
    }
    static bool watchdog_expired() {
        if (!g_kernel_state.watchdog_enabled) return false;
        uint64_t elapsed_us = current_time_us_ - g_kernel_state.watchdog_last_feed_us;
        return elapsed_us > (g_kernel_state.watchdog_timeout_ms * 1000ULL);
    }

    // --- Performance Counters ---
    static uint32_t cycle_count() {
        return static_cast<uint32_t>(current_time_us_);
    }
    static uint32_t cycles_per_usec() { return 1; }

    // --- Reset state ---
    static void reset() {
        current_time_us_ = 0;
        timer_target_us_ = UINT64_MAX;
        in_critical_ = false;
        audio_muted_ = false;
        process_start_us_ = 0;
        simulated_process_us_ = 0;
        active_voices_ = 0;
        active_events_ = 0;
        current_budget_us_ = 0;
        hw_state_ptr_ = nullptr;

        g_kernel_state = KernelState{};
        // Update memory configuration from HW params
        g_kernel_state.update_from_hw_params();

        // Reset IRQ flags
        g_irq_flags = IrqFlagState{};

        // Reset task notifications
        g_task_notify = TaskNotification{};

        // Reset shared memory
        g_shared_mem.reset();

        // Re-initialize shared memory regions
        init_shared_memory();
    }

    // --- Task Scheduling Simulation ---
    // Simulates UMI-OS RTOS task execution and context switches.
    static inline uint32_t simulated_process_us_ = 0;
    static inline uint32_t active_voices_ = 0;
    static inline uint32_t active_events_ = 0;
    static inline uint32_t current_budget_us_ = 0;

    // Hardware state shared memory (updated by DriverServer)
    static inline HwStateShared* hw_state_ptr_ = nullptr;

    // Initialize shared memory regions (called once at startup)
    static void init_shared_memory() {
        // Allocate HW state region
        syscall(SysCall::SHM_ALLOC,
            SharedMemoryManager::REGION_HW_STATE,
            sizeof(HwStateShared),
            1);  // privileged
        hw_state_ptr_ = g_shared_mem.get_as<HwStateShared>(
            SharedMemoryManager::REGION_HW_STATE, true);

        // Allocate parameter table
        syscall(SysCall::SHM_ALLOC,
            SharedMemoryManager::REGION_PARAM_TABLE,
            256,  // 64 x 4-byte params
            0);   // user accessible
    }

    // Get HW state pointer (for ControlRunner to read)
    static const HwStateShared* get_hw_state() {
        return hw_state_ptr_;
    }

    // Switch to a task (simulates context switch)
    static void switch_to_task(TaskId task) {
        auto prev_task = g_kernel_state.current_task;
        if (prev_task != task) {
            // End previous task's run
            auto& prev = g_kernel_state.tasks[static_cast<uint8_t>(prev_task)];
            if (prev.state == TaskState::RUNNING) {
                prev.state = TaskState::READY;
            }
            // Start new task
            auto& next = g_kernel_state.tasks[static_cast<uint8_t>(task)];
            next.state = TaskState::RUNNING;
            next.last_run_us = current_time_us_;
            next.run_count++;
            g_kernel_state.current_task = task;
            g_kernel_state.context_switches++;
        }
    }

    // Block a task waiting for notification
    static void block_task(TaskId task, uint32_t wait_mask) {
        auto& t = g_kernel_state.tasks[static_cast<uint8_t>(task)];
        t.state = TaskState::BLOCKED;
        t.wait_reason = wait_mask;
    }

    // Wake a task if it has pending notifications
    static void try_wake_task(TaskId task) {
        auto& t = g_kernel_state.tasks[static_cast<uint8_t>(task)];
        if (t.state == TaskState::BLOCKED) {
            if (g_task_notify.has_pending(task, t.wait_reason)) {
                t.state = TaskState::READY;
                t.wait_reason = 0;
            }
        }
    }

    // Record task execution time
    static void record_task_time(TaskId task, uint32_t elapsed_us) {
        auto& t = g_kernel_state.tasks[static_cast<uint8_t>(task)];
        t.run_time_us += elapsed_us;
        g_kernel_state.total_run_time_us += elapsed_us;

        // Simulate stack usage (increases slightly with activity)
        uint32_t stack_estimate = t.stack_high_water + (elapsed_us / 100);
        if (stack_estimate > t.stack_size) {
            stack_estimate = t.stack_size;
        }
        if (stack_estimate > t.stack_high_water) {
            t.stack_high_water = static_cast<uint16_t>(stack_estimate);
        }
    }

    // Called at start of audio processing (I2S half-transfer IRQ simulation)
    static void begin_process() {
        process_start_us_ = current_time_us_;
        simulated_process_us_ = 0;
        active_events_ = 0;

        // Simulate DMA half-transfer IRQ
        // 1. ISR sets flag
        g_irq_flags.set(IrqFlag::DMA_HALF_TRANSFER);
        g_kernel_state.irq_count++;
        g_kernel_state.audio_irq_count++;
        g_kernel_state.dma_irq_count++;

        // 2. ISR notifies AudioRunner
        g_task_notify.notify(TaskId::AUDIO_RUNNER, TaskNotification::NOTIFY_AUDIO_READY);

        // 3. AudioRunner wakes and preempts (highest priority)
        try_wake_task(TaskId::AUDIO_RUNNER);
        switch_to_task(TaskId::AUDIO_RUNNER);

        // 4. AudioRunner clears the flag
        g_irq_flags.test_and_clear(IrqFlag::DMA_HALF_TRANSFER);
    }

    // Set number of active voices for DSP load simulation
    static void set_active_voices(uint32_t count) {
        active_voices_ = count;
    }

    // Add event processing cost
    static void add_event_cost(uint32_t count = 1) {
        active_events_ += count;
    }

    // End audio process and calculate simulated DSP load
    static void end_process(uint32_t budget_us, uint32_t buffer_size) {
        current_budget_us_ = budget_us;

        // Simulate processing time based on configurable HW parameters:
        // 1. ISR overhead (interrupt entry/exit, DMA setup)
        // 2. Per-sample base cost (buffer copy, mixing)
        // 3. Per-voice per-sample cost (oscillator + filter + envelope)
        // 4. Per-event cost (MIDI parsing, note allocation)
        //
        // All timing is calculated from CPU cycles using configurable CPU frequency.
        // Default: ARM Cortex-M4 @ 168MHz

        const auto& hw = g_hw_sim_params;

        // Calculate total cycles
        uint64_t total_cycles =
            static_cast<uint64_t>(hw.isr_overhead_cycles) +
            static_cast<uint64_t>(buffer_size) * hw.base_cycles_per_sample +
            static_cast<uint64_t>(buffer_size) * active_voices_ * hw.voice_cycles_per_sample +
            static_cast<uint64_t>(active_events_) * hw.event_cycles;

        // Convert cycles to microseconds: us = cycles / MHz
        simulated_process_us_ = static_cast<uint32_t>(total_cycles / hw.cpu_freq_mhz);

        // Record Audio Runner task execution time
        record_task_time(TaskId::AUDIO_RUNNER, simulated_process_us_);

        // Calculate idle time (budget - processing time)
        if (budget_us > simulated_process_us_) {
            uint32_t idle_us = budget_us - simulated_process_us_;
            g_kernel_state.idle_time_us += idle_us;
            g_kernel_state.tasks[static_cast<uint8_t>(TaskId::IDLE)].run_time_us += idle_us;
        }

        // Calculate load percentage (x100 for 0.01% precision)
        // DSP Load = Audio Runner execution time / audio buffer period
        uint32_t load_x100 = 0;
        if (budget_us > 0) {
            load_x100 = static_cast<uint32_t>(
                (static_cast<uint64_t>(simulated_process_us_) * 10000) / budget_us
            );
        }

        // Cap at 100% (10000)
        if (load_x100 > 10000) {
            load_x100 = 10000;
            g_kernel_state.audio_drop_count++;  // Buffer overrun
        }

        g_kernel_state.dsp_load_percent_x100 = load_x100;

        if (load_x100 > g_kernel_state.dsp_load_peak_x100) {
            g_kernel_state.dsp_load_peak_x100 = load_x100;
        }

        // Return to Control Runner (or Idle if nothing to do)
        switch_to_task(TaskId::CONTROL_RUNNER);
    }

    // Simulate Control Runner tick (1ms timeslice)
    static void tick_control_runner(uint32_t elapsed_us = 1000) {
        // Control Runner gets a 1ms timeslice
        constexpr uint32_t CTRL_OVERHEAD_US = 50;  // Coroutine scheduler overhead

        // Simulate some control processing
        uint32_t ctrl_time = CTRL_OVERHEAD_US;
        record_task_time(TaskId::CONTROL_RUNNER, ctrl_time);

        // Remaining time goes to idle
        if (elapsed_us > ctrl_time) {
            g_kernel_state.idle_time_us += (elapsed_us - ctrl_time);
        }
    }

    // Simulate Driver Server tick (called periodically for HW state update)
    static void tick_driver_server() {
        // DriverServer updates hardware state:
        // - ADC readings (potentiometers, CV inputs)
        // - GPIO state (buttons, LEDs)
        // - Battery monitoring
        // - Temperature sensor
        // Typical execution: 10-50us depending on peripherals

        constexpr uint32_t DRIVER_OVERHEAD_US = 30;

        // Timer IRQ triggers
        g_irq_flags.set(IrqFlag::TIMER2);
        g_kernel_state.timer_irq_count++;
        g_kernel_state.irq_count++;

        // Notify DriverServer
        g_task_notify.notify(TaskId::DRIVER_SERVER, TaskNotification::NOTIFY_TIMER);
        try_wake_task(TaskId::DRIVER_SERVER);

        // Context switch to Driver Server
        switch_to_task(TaskId::DRIVER_SERVER);

        // DriverServer reads HW and updates shared memory
        if (hw_state_ptr_) {
            // Simulate ADC conversion complete
            g_irq_flags.set(IrqFlag::ADC_COMPLETE);

            // Update HW state in shared memory
            hw_state_ptr_->update_tick = static_cast<uint32_t>(current_time_us_ / 1000);
            hw_state_ptr_->sequence++;

            // Update battery from kernel state
            hw_state_ptr_->battery_mv = g_kernel_state.battery_voltage_mv;
            hw_state_ptr_->battery_percent = g_kernel_state.battery_percent;
            hw_state_ptr_->battery_flags =
                (g_kernel_state.battery_charging ? 0x01 : 0) |
                (g_kernel_state.battery_percent < 20 ? 0x02 : 0);
        }

        record_task_time(TaskId::DRIVER_SERVER, DRIVER_OVERHEAD_US);

        // Notify ControlRunner that HW state was updated
        g_task_notify.notify(TaskId::CONTROL_RUNNER, TaskNotification::NOTIFY_HW_UPDATE);
        try_wake_task(TaskId::CONTROL_RUNNER);

        // Clear timer flag
        g_irq_flags.test_and_clear(IrqFlag::TIMER2);
        g_irq_flags.test_and_clear(IrqFlag::ADC_COMPLETE);

        // Return to Idle (or ControlRunner if it has work)
        if (g_kernel_state.tasks[static_cast<uint8_t>(TaskId::CONTROL_RUNNER)].state == TaskState::READY) {
            switch_to_task(TaskId::CONTROL_RUNNER);
        } else {
            switch_to_task(TaskId::IDLE);
        }
    }

    // Simulate SysTick handler (1ms tick)
    static void tick_systick() {
        // SysTick IRQ
        g_irq_flags.set(IrqFlag::SYS_TICK);
        g_kernel_state.systick_count++;
        g_kernel_state.irq_count++;

        // Clear SysTick flag (typically done immediately in ISR)
        g_irq_flags.test_and_clear(IrqFlag::SYS_TICK);

        // Every 10ms, run DriverServer for HW updates
        if (g_kernel_state.systick_count % 10 == 0) {
            tick_driver_server();
        }
    }

    // Simulate GPIO external interrupt
    static void trigger_gpio_irq(uint32_t pin_mask) {
        g_irq_flags.set(IrqFlag::GPIO_EXTI);
        g_kernel_state.irq_count++;

        // Notify ControlRunner for button/switch handling
        g_task_notify.notify(TaskId::CONTROL_RUNNER, TaskNotification::NOTIFY_HW_UPDATE);
        try_wake_task(TaskId::CONTROL_RUNNER);

        // Update GPIO input state in shared memory
        if (hw_state_ptr_) {
            hw_state_ptr_->gpio_input = pin_mask;
            hw_state_ptr_->sequence++;
        }
    }

    // Simulate ADC update (potentiometer/CV change)
    static void update_adc(uint8_t channel, uint16_t value) {
        if (hw_state_ptr_ && channel < 8) {
            hw_state_ptr_->adc_values[channel] = value;
            hw_state_ptr_->sequence++;
        }
    }

    // Simulate encoder update
    static void update_encoder(uint8_t encoder, int16_t position) {
        if (hw_state_ptr_ && encoder < 4) {
            hw_state_ptr_->encoder_pos[encoder] = position;
            hw_state_ptr_->sequence++;
        }
    }

    // Simulate heap allocation (for memory tracking)
    static void* sim_malloc(size_t size) {
        uint32_t new_used = g_kernel_state.heap_used + static_cast<uint32_t>(size);
        if (new_used > g_kernel_state.heap_total) {
            g_kernel_state.malloc_fail_count++;
            return nullptr;
        }
        g_kernel_state.heap_used = new_used;
        if (new_used > g_kernel_state.heap_peak) {
            g_kernel_state.heap_peak = new_used;
        }
        return reinterpret_cast<void*>(1);  // Dummy pointer
    }

    static void sim_free(size_t size) {
        if (g_kernel_state.heap_used >= size) {
            g_kernel_state.heap_used -= static_cast<uint32_t>(size);
        }
    }

    // Get simulated processing time (for debugging/display)
    static uint32_t get_simulated_process_us() {
        return simulated_process_us_;
    }

    // Get CPU utilization (non-idle time / total time)
    static uint32_t get_cpu_utilization_x100() {
        if (g_kernel_state.uptime_us == 0) return 0;
        uint64_t busy_time = g_kernel_state.total_run_time_us;
        return static_cast<uint32_t>((busy_time * 10000) / g_kernel_state.uptime_us);
    }

    // --- Battery Simulation ---
    static void set_battery_state(uint8_t percent, bool charging, uint16_t voltage_mv) {
        g_kernel_state.battery_percent = percent;
        g_kernel_state.battery_charging = charging;
        g_kernel_state.battery_voltage_mv = voltage_mv;
    }

    // --- RTC ---
    static void set_rtc(uint64_t epoch_sec) {
        g_kernel_state.rtc_epoch_sec = epoch_sec;
    }
    static uint64_t get_rtc() {
        return g_kernel_state.rtc_epoch_sec + (current_time_us_ / 1000000);
    }
};

// Type alias for umios compatibility
using HW = umi::Hw<WebHwImpl>;

// ============================================================================
// WebSimAdapter - Adapter connecting Processor to WASM exports
// ============================================================================
// Uses umios types directly (AudioContext, EventQueue, Event)

struct WebSimConfig {
    uint32_t sample_rate = 48000;
    uint32_t buffer_size = 128;
    uint8_t num_inputs = 0;
    uint8_t num_outputs = 1;
};

template<typename Processor, WebSimConfig Config = WebSimConfig{}>
class WebSimAdapter {
public:
    WebSimAdapter() = default;

    void init() {
        sample_rate_ = Config.sample_rate;
        dt_ = 1.0f / static_cast<float>(sample_rate_);
        sample_position_ = 0;
        processor_.init(static_cast<float>(sample_rate_));
        WebHwImpl::reset();
        initialized_ = true;
    }

    void reset() {
        sample_position_ = 0;
        input_events_.clear();
        output_events_.clear();
        WebHwImpl::reset();
        if (initialized_) {
            processor_.init(static_cast<float>(sample_rate_));
        }
    }

    void process(const float* input, float* output, uint32_t frames, uint32_t sample_rate) {
        // Handle sample rate change
        if (sample_rate != sample_rate_) {
            sample_rate_ = sample_rate;
            dt_ = 1.0f / static_cast<float>(sample_rate_);
            processor_.init(static_cast<float>(sample_rate_));
        }

        if (!initialized_) {
            init();
        }

        // Setup buffer pointers
        input_ptrs_[0] = input;
        output_ptrs_[0] = output;

        // Snapshot input events
        process_input_events(frames);

        // Build AudioContext using umios types
        umi::AudioContext ctx{
            .inputs = std::span<const sample_t* const>(input_ptrs_.data(), Config.num_inputs),
            .outputs = std::span<sample_t* const>(output_ptrs_.data(), Config.num_outputs),
            .input_events = std::span<const umi::Event>(input_events_snapshot_.data(), input_events_count_),
            .output_events = output_events_,
            .sample_rate = sample_rate_,
            .buffer_size = frames,
            .dt = dt_,
            .sample_position = sample_position_,
        };

        // Clear output buffer
        for (uint32_t i = 0; i < frames; ++i) {
            output[i] = 0.0f;
        }

        // Calculate audio budget in microseconds
        uint64_t budget_us = (frames * 1000000ULL) / sample_rate_;

        // Start DSP load measurement
        WebHwImpl::begin_process();
        WebHwImpl::add_event_cost(static_cast<uint32_t>(input_events_count_));

        // Process audio
        processor_.process(ctx);

        // Count active voices for DSP load simulation
        uint32_t active_voices = count_active_voices();
        WebHwImpl::set_active_voices(active_voices);

        // Advance time simulation
        sample_position_ += frames;
        WebHwImpl::advance_time(budget_us);

        // End DSP measurement with buffer size for accurate load calculation
        WebHwImpl::end_process(static_cast<uint32_t>(budget_us), frames);

        // Update kernel state
        g_kernel_state.uptime_us = WebHwImpl::current_time_us_;
        g_kernel_state.audio_buffer_count++;
        g_kernel_state.audio_running = true;

        // Simulate SysTick interrupts for the elapsed time (1ms per tick)
        uint32_t ticks = static_cast<uint32_t>(budget_us / 1000);
        for (uint32_t i = 0; i < ticks; ++i) {
            WebHwImpl::tick_systick();
        }

        // Feed watchdog (simulating normal operation)
        WebHwImpl::watchdog_feed();

        // Clear processed events
        input_events_count_ = 0;
    }

    // MIDI input via umios Event
    void send_midi(uint8_t status, uint8_t data1, uint8_t data2, uint32_t sample_offset = 0) {
        umi::Event ev = umi::Event::make_midi(0, sample_offset, status, data1, data2);
        (void)input_events_.push(ev);
        g_kernel_state.midi_rx_count++;
    }

    void note_on(uint8_t note, uint8_t velocity, uint8_t channel = 0) {
        send_midi(0x90 | (channel & 0x0F), note, velocity);
    }

    void note_off(uint8_t note, uint8_t velocity = 0, uint8_t channel = 0) {
        send_midi(0x80 | (channel & 0x0F), note, velocity);
    }

    void control_change(uint8_t cc, uint8_t value, uint8_t channel = 0) {
        send_midi(0xB0 | (channel & 0x0F), cc, value);
    }

    // Accessors
    uint64_t sample_position() const { return sample_position_; }
    uint32_t sample_rate() const { return sample_rate_; }
    Processor& processor() { return processor_; }
    const Processor& processor() const { return processor_; }

    // Count active voices for DSP load simulation
    uint32_t count_active_voices() const {
        return get_active_voices_impl(nullptr);
    }

private:
    // SFINAE helper: use this if processor has active_voice_count()
    template<typename P = Processor>
    auto get_active_voices_impl(int*) const
        -> decltype(std::declval<const P&>().active_voice_count()) {
        return processor_.active_voice_count();
    }

    // Fallback: use this if processor doesn't have active_voice_count()
    uint32_t get_active_voices_impl(...) const {
        return 1;  // Assume 1 voice as default
    }

public:

    // Parameter access (delegates to processor if it has params)
    template<typename P = Processor>
    auto set_param(uint32_t id, float value) -> decltype(std::declval<P&>().set_param(id, value)) {
        processor_.set_param(id, value);
    }

    template<typename P = Processor>
    auto get_param(uint32_t id) const -> decltype(std::declval<const P&>().get_param(id)) {
        return processor_.get_param(id);
    }

    template<typename P = Processor>
    static auto param_count() -> decltype(P::param_count()) {
        return P::param_count();
    }

    template<typename P = Processor>
    static auto get_param_descriptor(uint32_t id) -> decltype(P::get_param_descriptor(id)) {
        return P::get_param_descriptor(id);
    }

private:
    void process_input_events(uint32_t frames) {
        input_events_count_ = 0;
        umi::Event ev;
        // Pop events in FIFO order
        while (input_events_.pop(ev) && input_events_count_ < input_events_snapshot_.size()) {
            if (ev.sample_pos >= frames) {
                ev.sample_pos = frames - 1;
            }
            input_events_snapshot_[input_events_count_++] = ev;
        }

        // Sort by sample position (insertion sort for small arrays)
        for (size_t i = 1; i < input_events_count_; ++i) {
            umi::Event key = input_events_snapshot_[i];
            size_t j = i;
            while (j > 0 && input_events_snapshot_[j-1].sample_pos > key.sample_pos) {
                input_events_snapshot_[j] = input_events_snapshot_[j-1];
                --j;
            }
            input_events_snapshot_[j] = key;
        }
    }

private:
    Processor processor_{};
    uint32_t sample_rate_ = Config.sample_rate;
    float dt_ = 1.0f / Config.sample_rate;
    uint64_t sample_position_ = 0;
    bool initialized_ = false;

    // Buffer pointers
    std::array<const sample_t*, 2> input_ptrs_{};
    std::array<sample_t*, 2> output_ptrs_{};

    // Event queues (using umios EventQueue with default size)
    umi::EventQueue<> input_events_;
    umi::EventQueue<> output_events_;

    // Event snapshot for processing
    std::array<umi::Event, umi::MAX_EVENTS_PER_BUFFER> input_events_snapshot_{};
    size_t input_events_count_ = 0;
};

// ============================================================================
// WASM Export Macros
// ============================================================================

#define UMI_WEB_SIM_EXPORT(ProcessorType) \
    static umi::web::WebSimAdapter<ProcessorType> g_web_sim; \
    \
    UMI_WEB_EXPORT void umi_sim_init(void) { \
        g_web_sim.init(); \
    } \
    UMI_WEB_EXPORT void umi_sim_reset(void) { \
        g_web_sim.reset(); \
    } \
    UMI_WEB_EXPORT void umi_sim_process(const float* in, float* out, \
                                         uint32_t frames, uint32_t sr) { \
        g_web_sim.process(in, out, frames, sr); \
    } \
    UMI_WEB_EXPORT void umi_sim_note_on(uint8_t note, uint8_t vel) { \
        g_web_sim.note_on(note, vel); \
    } \
    UMI_WEB_EXPORT void umi_sim_note_off(uint8_t note) { \
        g_web_sim.note_off(note); \
    } \
    UMI_WEB_EXPORT void umi_sim_cc(uint8_t cc, uint8_t value) { \
        g_web_sim.control_change(cc, value); \
    } \
    UMI_WEB_EXPORT void umi_sim_midi(uint8_t status, uint8_t d1, uint8_t d2) { \
        g_web_sim.send_midi(status, d1, d2); \
    } \
    UMI_WEB_EXPORT float umi_sim_load(void) { \
        return 0.0f; /* TODO: measure actual load */ \
    } \
    UMI_WEB_EXPORT uint32_t umi_sim_position_lo(void) { \
        return static_cast<uint32_t>(g_web_sim.sample_position()); \
    } \
    UMI_WEB_EXPORT uint32_t umi_sim_position_hi(void) { \
        return static_cast<uint32_t>(g_web_sim.sample_position() >> 32); \
    } \
    UMI_WEB_EXPORT uint32_t umi_sim_sample_rate(void) { \
        return g_web_sim.sample_rate(); \
    }

#define UMI_WEB_SIM_EXPORT_PARAMS(ProcessorType) \
    UMI_WEB_EXPORT uint32_t umi_sim_param_count(void) { \
        return ProcessorType::param_count(); \
    } \
    UMI_WEB_EXPORT void umi_sim_set_param(uint32_t id, float value) { \
        g_web_sim.processor().set_param(id, value); \
    } \
    UMI_WEB_EXPORT float umi_sim_get_param(uint32_t id) { \
        return g_web_sim.processor().get_param(id); \
    } \
    UMI_WEB_EXPORT const char* umi_sim_param_name(uint32_t id) { \
        auto* desc = ProcessorType::get_param_descriptor(id); \
        return desc ? desc->name.data() : ""; \
    } \
    UMI_WEB_EXPORT float umi_sim_param_min(uint32_t id) { \
        auto* desc = ProcessorType::get_param_descriptor(id); \
        return desc ? desc->min_value : 0.0f; \
    } \
    UMI_WEB_EXPORT float umi_sim_param_max(uint32_t id) { \
        auto* desc = ProcessorType::get_param_descriptor(id); \
        return desc ? desc->max_value : 1.0f; \
    } \
    UMI_WEB_EXPORT float umi_sim_param_default(uint32_t id) { \
        auto* desc = ProcessorType::get_param_descriptor(id); \
        return desc ? desc->default_value : 0.0f; \
    }

#define UMI_WEB_SIM_EXPORT_NAMED(ProcessorType, name, vendor, version) \
    UMI_WEB_SIM_EXPORT(ProcessorType) \
    UMI_WEB_SIM_EXPORT_PARAMS(ProcessorType) \
    UMI_WEB_EXPORT const char* umi_sim_get_name(void) { return name; } \
    UMI_WEB_EXPORT const char* umi_sim_get_vendor(void) { return vendor; } \
    UMI_WEB_EXPORT const char* umi_sim_get_version(void) { return version; }

// ============================================================================
// Kernel State Exports
// ============================================================================

#define UMI_WEB_SIM_EXPORT_KERNEL() \
    /* --- Time --- */ \
    UMI_WEB_EXPORT uint32_t umi_kernel_uptime_lo(void) { \
        return static_cast<uint32_t>(umi::web::g_kernel_state.uptime_us); \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_uptime_hi(void) { \
        return static_cast<uint32_t>(umi::web::g_kernel_state.uptime_us >> 32); \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_rtc_lo(void) { \
        return static_cast<uint32_t>(umi::web::WebHwImpl::get_rtc()); \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_rtc_hi(void) { \
        return static_cast<uint32_t>(umi::web::WebHwImpl::get_rtc() >> 32); \
    } \
    UMI_WEB_EXPORT void umi_kernel_set_rtc(uint32_t lo, uint32_t hi) { \
        uint64_t epoch = (static_cast<uint64_t>(hi) << 32) | lo; \
        umi::web::WebHwImpl::set_rtc(epoch); \
    } \
    \
    /* --- Audio --- */ \
    UMI_WEB_EXPORT uint32_t umi_kernel_audio_buffers(void) { \
        return umi::web::g_kernel_state.audio_buffer_count; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_audio_drops(void) { \
        return umi::web::g_kernel_state.audio_drop_count; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_dsp_load(void) { \
        return umi::web::g_kernel_state.dsp_load_percent_x100; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_dsp_peak(void) { \
        return umi::web::g_kernel_state.dsp_load_peak_x100; \
    } \
    UMI_WEB_EXPORT void umi_kernel_reset_dsp_peak(void) { \
        umi::web::g_kernel_state.dsp_load_peak_x100 = 0; \
    } \
    UMI_WEB_EXPORT uint8_t umi_kernel_audio_running(void) { \
        return umi::web::g_kernel_state.audio_running ? 1 : 0; \
    } \
    \
    /* --- MIDI --- */ \
    UMI_WEB_EXPORT uint32_t umi_kernel_midi_rx(void) { \
        return umi::web::g_kernel_state.midi_rx_count; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_midi_tx(void) { \
        return umi::web::g_kernel_state.midi_tx_count; \
    } \
    \
    /* --- Power --- */ \
    UMI_WEB_EXPORT uint8_t umi_kernel_battery_percent(void) { \
        return umi::web::g_kernel_state.battery_percent; \
    } \
    UMI_WEB_EXPORT uint8_t umi_kernel_battery_charging(void) { \
        return umi::web::g_kernel_state.battery_charging ? 1 : 0; \
    } \
    UMI_WEB_EXPORT uint8_t umi_kernel_usb_connected(void) { \
        return umi::web::g_kernel_state.usb_connected ? 1 : 0; \
    } \
    UMI_WEB_EXPORT uint16_t umi_kernel_battery_voltage(void) { \
        return umi::web::g_kernel_state.battery_voltage_mv; \
    } \
    UMI_WEB_EXPORT void umi_kernel_set_battery(uint8_t percent, uint8_t charging, uint16_t voltage) { \
        umi::web::WebHwImpl::set_battery_state(percent, charging != 0, voltage); \
    } \
    \
    /* --- Watchdog --- */ \
    UMI_WEB_EXPORT uint8_t umi_kernel_watchdog_enabled(void) { \
        return umi::web::g_kernel_state.watchdog_enabled ? 1 : 0; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_watchdog_timeout(void) { \
        return umi::web::g_kernel_state.watchdog_timeout_ms; \
    } \
    UMI_WEB_EXPORT uint8_t umi_kernel_watchdog_expired(void) { \
        return umi::web::WebHwImpl::watchdog_expired() ? 1 : 0; \
    } \
    UMI_WEB_EXPORT void umi_kernel_watchdog_init(uint32_t timeout_ms) { \
        umi::web::WebHwImpl::watchdog_init(timeout_ms); \
    } \
    UMI_WEB_EXPORT void umi_kernel_watchdog_feed(void) { \
        umi::web::WebHwImpl::watchdog_feed(); \
    } \
    \
    /* --- Tasks --- */ \
    UMI_WEB_EXPORT uint8_t umi_kernel_task_count(void) { \
        return umi::web::g_kernel_state.task_count(); \
    } \
    UMI_WEB_EXPORT uint8_t umi_kernel_task_ready(void) { \
        return umi::web::g_kernel_state.task_ready_count(); \
    } \
    UMI_WEB_EXPORT uint8_t umi_kernel_task_blocked(void) { \
        return umi::web::g_kernel_state.task_blocked_count(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_context_switches(void) { \
        return umi::web::g_kernel_state.context_switches; \
    } \
    UMI_WEB_EXPORT uint8_t umi_kernel_current_task(void) { \
        return static_cast<uint8_t>(umi::web::g_kernel_state.current_task); \
    } \
    UMI_WEB_EXPORT const char* umi_kernel_task_name(uint8_t id) { \
        if (id < umi::web::g_kernel_state.MAX_TASKS) { \
            return umi::web::g_kernel_state.tasks[id].name; \
        } \
        return ""; \
    } \
    UMI_WEB_EXPORT uint8_t umi_kernel_task_state(uint8_t id) { \
        if (id < umi::web::g_kernel_state.MAX_TASKS) { \
            return static_cast<uint8_t>(umi::web::g_kernel_state.tasks[id].state); \
        } \
        return 0; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_task_run_time_lo(uint8_t id) { \
        if (id < umi::web::g_kernel_state.MAX_TASKS) { \
            return static_cast<uint32_t>(umi::web::g_kernel_state.tasks[id].run_time_us); \
        } \
        return 0; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_task_run_time_hi(uint8_t id) { \
        if (id < umi::web::g_kernel_state.MAX_TASKS) { \
            return static_cast<uint32_t>(umi::web::g_kernel_state.tasks[id].run_time_us >> 32); \
        } \
        return 0; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_task_run_count(uint8_t id) { \
        if (id < umi::web::g_kernel_state.MAX_TASKS) { \
            return umi::web::g_kernel_state.tasks[id].run_count; \
        } \
        return 0; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_idle_time_lo(void) { \
        return static_cast<uint32_t>(umi::web::g_kernel_state.idle_time_us); \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_idle_time_hi(void) { \
        return static_cast<uint32_t>(umi::web::g_kernel_state.idle_time_us >> 32); \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_cpu_util(void) { \
        return umi::web::WebHwImpl::get_cpu_utilization_x100(); \
    } \
    \
    /* --- Memory --- */ \
    UMI_WEB_EXPORT uint32_t umi_kernel_heap_used(void) { \
        return umi::web::g_kernel_state.heap_used; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_heap_total(void) { \
        return umi::web::g_kernel_state.heap_total; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_heap_peak(void) { \
        return umi::web::g_kernel_state.heap_peak; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_stack_used(void) { \
        return umi::web::g_kernel_state.total_stack_used(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_stack_total(void) { \
        return umi::web::g_kernel_state.total_stack_size(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_sram_total(void) { \
        return umi::web::g_kernel_state.sram_total; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_flash_total(void) { \
        return umi::web::g_kernel_state.flash_total; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_flash_used(void) { \
        return umi::web::g_kernel_state.flash_used; \
    } \
    \
    /* --- IRQ Statistics --- */ \
    UMI_WEB_EXPORT uint32_t umi_kernel_irq_count(void) { \
        return umi::web::g_kernel_state.irq_count; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_audio_irq_count(void) { \
        return umi::web::g_kernel_state.audio_irq_count; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_timer_irq_count(void) { \
        return umi::web::g_kernel_state.timer_irq_count; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_dma_irq_count(void) { \
        return umi::web::g_kernel_state.dma_irq_count; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_systick_count(void) { \
        return umi::web::g_kernel_state.systick_count; \
    } \
    \
    /* --- Error Statistics --- */ \
    UMI_WEB_EXPORT uint32_t umi_kernel_hardfault_count(void) { \
        return umi::web::g_kernel_state.hardfault_count; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_stack_overflow_count(void) { \
        return umi::web::g_kernel_state.stack_overflow_count; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_malloc_fail_count(void) { \
        return umi::web::g_kernel_state.malloc_fail_count; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_watchdog_reset_count(void) { \
        return umi::web::g_kernel_state.watchdog_reset_count; \
    } \
    \
    /* --- Log --- */ \
    UMI_WEB_EXPORT uint8_t umi_kernel_log_level(void) { \
        return umi::web::g_kernel_state.log_level; \
    } \
    UMI_WEB_EXPORT void umi_kernel_set_log_level(uint8_t level) { \
        umi::web::g_kernel_state.log_level = level; \
    } \
    UMI_WEB_EXPORT uint32_t umi_kernel_log_count(void) { \
        return umi::web::g_kernel_state.log_count; \
    } \
    \
    /* --- System --- */ \
    UMI_WEB_EXPORT void umi_kernel_reset(void) { \
        umi::web::WebHwImpl::system_reset(); \
    } \
    \
    /* --- SysEx IO --- */ \
    UMI_WEB_EXPORT uint32_t umi_sysex_available(void) { \
        return static_cast<uint32_t>(umi::web::g_sysex_log.available()); \
    } \
    UMI_WEB_EXPORT uint32_t umi_sysex_read(uint8_t* out, uint32_t max_len) { \
        return static_cast<uint32_t>(umi::web::g_sysex_log.read(out, max_len)); \
    } \
    UMI_WEB_EXPORT void umi_sysex_clear(void) { \
        umi::web::g_sysex_log.clear(); \
    } \
    UMI_WEB_EXPORT void umi_log_write(const char* msg, uint32_t len) { \
        umi::web::g_sysex_log.write_stdout(msg, len); \
    } \
    \
    /* --- Shell --- */ \
    UMI_WEB_EXPORT void umi_shell_input(const char* data, uint32_t len) { \
        for (uint32_t i = 0; i < len; i++) { \
            umi::web::g_shell_input.receive_char(data[i]); \
        } \
    } \
    UMI_WEB_EXPORT uint8_t umi_shell_has_command(void) { \
        return umi::web::g_shell_input.cmd_ready ? 1 : 0; \
    } \
    UMI_WEB_EXPORT const char* umi_shell_get_command(void) { \
        return umi::web::g_shell_input.get_command(); \
    } \
    UMI_WEB_EXPORT const char* umi_shell_execute(void) { \
        const char* cmd = umi::web::g_shell_input.get_command(); \
        if (!cmd) return ""; \
        const char* result = umi::web::g_new_shell.execute(cmd); \
        umi::web::g_shell_input.clear_command(); \
        /* Handle special commands */ \
        if (strcmp(result, "RESET_REQUESTED") == 0) { \
            umi::web::WebHwImpl::reset(); \
            return "Reset complete"; \
        } \
        return result; \
    } \
    \
    /* --- IRQ Flags --- */ \
    UMI_WEB_EXPORT uint8_t umi_irq_flag_pending(uint8_t flag) { \
        if (flag >= static_cast<uint8_t>(umi::web::IrqFlag::COUNT)) return 0; \
        return umi::web::g_irq_flags.test(static_cast<umi::web::IrqFlag>(flag)) ? 1 : 0; \
    } \
    UMI_WEB_EXPORT uint32_t umi_irq_flag_count(uint8_t flag) { \
        if (flag >= static_cast<uint8_t>(umi::web::IrqFlag::COUNT)) return 0; \
        return umi::web::g_irq_flags.get_count(static_cast<umi::web::IrqFlag>(flag)); \
    } \
    UMI_WEB_EXPORT void umi_irq_trigger(uint8_t flag) { \
        if (flag >= static_cast<uint8_t>(umi::web::IrqFlag::COUNT)) return; \
        umi::web::g_irq_flags.set(static_cast<umi::web::IrqFlag>(flag)); \
    } \
    \
    /* --- Task Notifications --- */ \
    UMI_WEB_EXPORT uint32_t umi_notify_pending(uint8_t task) { \
        if (task >= static_cast<uint8_t>(umi::web::TaskId::COUNT)) return 0; \
        return umi::web::g_task_notify.values[task]; \
    } \
    UMI_WEB_EXPORT void umi_notify_send(uint8_t task, uint32_t bits) { \
        if (task >= static_cast<uint8_t>(umi::web::TaskId::COUNT)) return; \
        umi::web::g_task_notify.notify(static_cast<umi::web::TaskId>(task), bits); \
    } \
    UMI_WEB_EXPORT uint32_t umi_task_wait_reason(uint8_t task) { \
        if (task >= static_cast<uint8_t>(umi::web::TaskId::COUNT)) return 0; \
        return umi::web::g_kernel_state.tasks[task].wait_reason; \
    } \
    \
    /* --- Shared Memory --- */ \
    UMI_WEB_EXPORT int32_t umi_shm_allocate(uint8_t region_id, uint32_t size, uint8_t privileged) { \
        auto result = umi::web::syscall( \
            umi::web::SysCall::SHM_ALLOC, region_id, size, privileged); \
        return result.ret; \
    } \
    UMI_WEB_EXPORT uint32_t umi_shm_size(uint8_t region_id) { \
        return static_cast<uint32_t>(umi::web::g_shared_mem.get_size( \
            static_cast<umi::web::SharedMemoryManager::RegionId>(region_id))); \
    } \
    UMI_WEB_EXPORT uint32_t umi_shm_pool_used(void) { \
        return static_cast<uint32_t>(umi::web::g_shared_mem.pool_used); \
    } \
    \
    /* --- HW State (from shared memory) --- */ \
    UMI_WEB_EXPORT uint32_t umi_hw_state_sequence(void) { \
        auto* hw = umi::web::WebHwImpl::get_hw_state(); \
        return hw ? hw->sequence : 0; \
    } \
    UMI_WEB_EXPORT uint16_t umi_hw_adc_value(uint8_t channel) { \
        auto* hw = umi::web::WebHwImpl::get_hw_state(); \
        if (!hw || channel >= 8) return 0; \
        return hw->adc_values[channel]; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_adc(uint8_t channel, uint16_t value) { \
        umi::web::WebHwImpl::update_adc(channel, value); \
    } \
    UMI_WEB_EXPORT int16_t umi_hw_encoder_pos(uint8_t encoder) { \
        auto* hw = umi::web::WebHwImpl::get_hw_state(); \
        if (!hw || encoder >= 4) return 0; \
        return hw->encoder_pos[encoder]; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_encoder(uint8_t encoder, int16_t pos) { \
        umi::web::WebHwImpl::update_encoder(encoder, pos); \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_gpio_input(void) { \
        auto* hw = umi::web::WebHwImpl::get_hw_state(); \
        return hw ? hw->gpio_input : 0; \
    } \
    UMI_WEB_EXPORT void umi_hw_trigger_gpio(uint32_t pin_mask) { \
        umi::web::WebHwImpl::trigger_gpio_irq(pin_mask); \
    } \
    \
    /* --- HW Simulation Parameters --- */ \
    UMI_WEB_EXPORT uint32_t umi_hw_cpu_freq(void) { \
        return umi::web::g_hw_sim_params.cpu_freq_mhz; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_cpu_freq(uint32_t mhz) { \
        umi::web::g_hw_sim_params.cpu_freq_mhz = mhz; \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_isr_overhead(void) { \
        return umi::web::g_hw_sim_params.isr_overhead_cycles; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_isr_overhead(uint32_t cycles) { \
        umi::web::g_hw_sim_params.isr_overhead_cycles = cycles; \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_base_cycles(void) { \
        return umi::web::g_hw_sim_params.base_cycles_per_sample; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_base_cycles(uint32_t cycles) { \
        umi::web::g_hw_sim_params.base_cycles_per_sample = cycles; \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_voice_cycles(void) { \
        return umi::web::g_hw_sim_params.voice_cycles_per_sample; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_voice_cycles(uint32_t cycles) { \
        umi::web::g_hw_sim_params.voice_cycles_per_sample = cycles; \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_event_cycles(void) { \
        return umi::web::g_hw_sim_params.event_cycles; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_event_cycles(uint32_t cycles) { \
        umi::web::g_hw_sim_params.event_cycles = cycles; \
    } \
    \
    /* --- Memory Configuration --- */ \
    UMI_WEB_EXPORT uint32_t umi_hw_sram_total_kb(void) { \
        return umi::web::g_hw_sim_params.sram_total_kb; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_sram_total_kb(uint32_t kb) { \
        umi::web::g_hw_sim_params.sram_total_kb = kb; \
        umi::web::g_kernel_state.update_from_hw_params(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_heap_size_kb(void) { \
        return umi::web::g_hw_sim_params.heap_size_kb; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_heap_size_kb(uint32_t kb) { \
        umi::web::g_hw_sim_params.heap_size_kb = kb; \
        umi::web::g_kernel_state.update_from_hw_params(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_task_stack_bytes(void) { \
        return umi::web::g_hw_sim_params.task_stack_bytes; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_task_stack_bytes(uint32_t bytes) { \
        umi::web::g_hw_sim_params.task_stack_bytes = bytes; \
        umi::web::g_kernel_state.update_from_hw_params(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_flash_total_kb(void) { \
        return umi::web::g_hw_sim_params.flash_total_kb; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_flash_total_kb(uint32_t kb) { \
        umi::web::g_hw_sim_params.flash_total_kb = kb; \
        umi::web::g_kernel_state.update_from_hw_params(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_flash_used_kb(void) { \
        return umi::web::g_hw_sim_params.flash_used_kb; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_flash_used_kb(uint32_t kb) { \
        umi::web::g_hw_sim_params.flash_used_kb = kb; \
        umi::web::g_kernel_state.update_from_hw_params(); \
    } \
    \
    /* --- Detailed Memory Configuration --- */ \
    UMI_WEB_EXPORT uint32_t umi_hw_data_bss_kb(void) { \
        return umi::web::g_hw_sim_params.data_bss_kb; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_data_bss_kb(uint32_t kb) { \
        umi::web::g_hw_sim_params.data_bss_kb = kb; \
        umi::web::g_hw_sim_params.recalculate_layout(); \
        umi::web::g_kernel_state.update_from_hw_params(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_main_stack_kb(void) { \
        return umi::web::g_hw_sim_params.main_stack_kb; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_main_stack_kb(uint32_t kb) { \
        umi::web::g_hw_sim_params.main_stack_kb = kb; \
        umi::web::g_hw_sim_params.recalculate_layout(); \
        umi::web::g_kernel_state.update_from_hw_params(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_task_count(void) { \
        return umi::web::g_hw_sim_params.task_count; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_task_count(uint32_t count) { \
        umi::web::g_hw_sim_params.task_count = count; \
        umi::web::g_hw_sim_params.recalculate_layout(); \
        umi::web::g_kernel_state.update_from_hw_params(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_dma_buffer_kb(void) { \
        return umi::web::g_hw_sim_params.dma_buffer_kb; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_dma_buffer_kb(uint32_t kb) { \
        umi::web::g_hw_sim_params.dma_buffer_kb = kb; \
        umi::web::g_hw_sim_params.recalculate_layout(); \
        umi::web::g_kernel_state.update_from_hw_params(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_shared_mem_kb(void) { \
        return umi::web::g_hw_sim_params.shared_mem_kb; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_shared_mem_kb(uint32_t kb) { \
        umi::web::g_hw_sim_params.shared_mem_kb = kb; \
        umi::web::g_hw_sim_params.recalculate_layout(); \
        umi::web::g_kernel_state.update_from_hw_params(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_ccm_total_kb(void) { \
        return umi::web::g_hw_sim_params.ccm_total_kb; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_ccm_total_kb(uint32_t kb) { \
        umi::web::g_hw_sim_params.ccm_total_kb = kb; \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_ccm_used_kb(void) { \
        return umi::web::g_hw_sim_params.ccm_used_kb; \
    } \
    UMI_WEB_EXPORT void umi_hw_set_ccm_used_kb(uint32_t kb) { \
        umi::web::g_hw_sim_params.ccm_used_kb = kb; \
    } \
    \
    /* --- Memory Layout Query --- */ \
    UMI_WEB_EXPORT uint32_t umi_mem_warning(void) { \
        return static_cast<uint32_t>(umi::web::g_memory_layout.warning); \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_collision_possible(void) { \
        return umi::web::g_memory_layout.collision_possible ? 1 : 0; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_sram_used(void) { \
        return umi::web::g_memory_layout.sram_used; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_sram_free(void) { \
        return umi::web::g_memory_layout.sram_free; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_free_gap(void) { \
        return umi::web::g_memory_layout.free_between_heap_stack; \
    } \
    /* Region: .data/.bss */ \
    UMI_WEB_EXPORT uint32_t umi_mem_data_bss_base(void) { \
        return umi::web::g_memory_layout.data_bss.base; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_data_bss_size(void) { \
        return umi::web::g_memory_layout.data_bss.size; \
    } \
    /* Region: Heap */ \
    UMI_WEB_EXPORT uint32_t umi_mem_heap_base(void) { \
        return umi::web::g_memory_layout.heap.base; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_heap_size(void) { \
        return umi::web::g_memory_layout.heap.size; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_heap_used(void) { \
        return umi::web::g_memory_layout.heap.used; \
    } \
    UMI_WEB_EXPORT void umi_mem_set_heap_used(uint32_t bytes) { \
        umi::web::g_memory_layout.heap.used = bytes; \
        if (bytes > umi::web::g_memory_layout.heap.peak) { \
            umi::web::g_memory_layout.heap.peak = bytes; \
        } \
        umi::web::g_memory_layout.update_warning(); \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_heap_peak(void) { \
        return umi::web::g_memory_layout.heap.peak; \
    } \
    /* Region: Task Stacks */ \
    UMI_WEB_EXPORT uint32_t umi_mem_task_stacks_base(void) { \
        return umi::web::g_memory_layout.task_stacks.base; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_task_stacks_size(void) { \
        return umi::web::g_memory_layout.task_stacks.size; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_task_stacks_used(void) { \
        return umi::web::g_memory_layout.task_stacks.used; \
    } \
    UMI_WEB_EXPORT void umi_mem_set_task_stacks_used(uint32_t bytes) { \
        umi::web::g_memory_layout.task_stacks.used = bytes; \
        if (bytes > umi::web::g_memory_layout.task_stacks.peak) { \
            umi::web::g_memory_layout.task_stacks.peak = bytes; \
        } \
        umi::web::g_memory_layout.update_warning(); \
    } \
    /* Region: Main Stack (MSP) */ \
    UMI_WEB_EXPORT uint32_t umi_mem_main_stack_base(void) { \
        return umi::web::g_memory_layout.main_stack.base; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_main_stack_size(void) { \
        return umi::web::g_memory_layout.main_stack.size; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_main_stack_used(void) { \
        return umi::web::g_memory_layout.main_stack.used; \
    } \
    UMI_WEB_EXPORT void umi_mem_set_main_stack_used(uint32_t bytes) { \
        umi::web::g_memory_layout.main_stack.used = bytes; \
        if (bytes > umi::web::g_memory_layout.main_stack.peak) { \
            umi::web::g_memory_layout.main_stack.peak = bytes; \
        } \
        umi::web::g_memory_layout.update_warning(); \
    } \
    /* Region: DMA Buffers */ \
    UMI_WEB_EXPORT uint32_t umi_mem_dma_base(void) { \
        return umi::web::g_memory_layout.dma_buffers.base; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_dma_size(void) { \
        return umi::web::g_memory_layout.dma_buffers.size; \
    } \
    /* Region: Shared Memory */ \
    UMI_WEB_EXPORT uint32_t umi_mem_shared_base(void) { \
        return umi::web::g_memory_layout.shared_mem.base; \
    } \
    UMI_WEB_EXPORT uint32_t umi_mem_shared_size(void) { \
        return umi::web::g_memory_layout.shared_mem.size; \
    } \
    /* Validation */ \
    UMI_WEB_EXPORT uint32_t umi_hw_mem_valid(void) { \
        return umi::web::g_hw_sim_params.is_valid() ? 1 : 0; \
    } \
    UMI_WEB_EXPORT uint32_t umi_hw_mem_allocated(void) { \
        return umi::web::g_hw_sim_params.sram_allocated_bytes(); \
    }

} // namespace umi::web
