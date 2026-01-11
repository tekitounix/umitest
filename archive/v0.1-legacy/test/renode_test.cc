// =============================================================================
// UMI-OS Comprehensive Renode Test Suite
// =============================================================================
//
// This test runs on actual Cortex-M4 emulation in Renode.
// Output goes to UART for verification by Robot Framework.
//
// Build: xmake f --firmware=y && xmake build renode_test
// Run:   renode renode/test.resc
//
// NOTE: This file is ARM-only. For IDE support, generate ARM compile_commands.json.
// =============================================================================

#ifndef __arm__
#ifdef __clang__
#pragma message("renode_test.cc is ARM-only; for IDE indexing run: xmake f --firmware=y && xmake project -k compile_commands")
#endif
#endif

#ifdef __clang__
#pragma GCC diagnostic ignored "-Wshorten-64-to-32"
#pragma GCC diagnostic ignored "-Wreturn-type-c-linkage"
#pragma GCC diagnostic ignored "-Wsection"
#endif

#include "../core/umi_kernel.hh"
#include "../core/umi_expected.hh"
#include "../core/umi_monitor.hh"
#include "../port/arm/cortex-m/common/vector_table.hh"
#include <cstdint>

// =============================================================================
// Minimal UART output for test results
// =============================================================================

namespace {

// STM32F4 USART2 registers (directly on sysbus)
volatile uint32_t* const USART2_SR  = reinterpret_cast<volatile uint32_t*>(0x40004400);
volatile uint32_t* const USART2_DR  = reinterpret_cast<volatile uint32_t*>(0x40004404);
volatile uint32_t* const USART2_BRR = reinterpret_cast<volatile uint32_t*>(0x40004408);
volatile uint32_t* const USART2_CR1 = reinterpret_cast<volatile uint32_t*>(0x4000440C);

// RCC APB1ENR for USART2 clock
volatile uint32_t* const RCC_APB1ENR = reinterpret_cast<volatile uint32_t*>(0x40023840);

void uart_init() {
    // Enable USART2 clock (bit 17)
    *RCC_APB1ENR |= (1 << 17);
    
    // Configure: 8N1, TX enable
    *USART2_BRR = 0x0683;  // 9600 baud @ 16MHz (default)
    *USART2_CR1 = (1 << 13) | (1 << 3);  // UE=1, TE=1
}

void uart_putc(char c) {
    while (!(*USART2_SR & (1 << 7))) {}  // Wait TXE
    *USART2_DR = c;
}

void uart_puts(const char* s) { 
    while (*s) uart_putc(*s++);
}

[[maybe_unused]]
void uart_puthex(uint32_t val) {
    const char hex[] = "0123456789ABCDEF";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(hex[(val >> i) & 0xF]);
    }
}

void uart_putnum(int val) {
    if (val < 0) { uart_putc('-'); val = -val; }
    if (val == 0) { uart_putc('0'); return; }
    
    char buf[12];
    int i = 0;
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) uart_putc(buf[--i]);
}

int test_count = 0;
int pass_count = 0;
int fail_count = 0;

void test_pass(const char* name) {
    test_count++;
    pass_count++;
    uart_puts("[PASS] ");
    uart_puts(name);
    uart_puts("\r\n");
}

void test_fail(const char* name, const char* reason) {
    test_count++;
    fail_count++;
    uart_puts("[FAIL] ");
    uart_puts(name);
    uart_puts(": ");
    uart_puts(reason);
    uart_puts("\r\n");
}

// Test assertion functions (no macros)
inline void test_assert(bool cond, const char* name, const char* expr) {
    if (cond) test_pass(name); 
    else test_fail(name, expr);
}

template<typename T, typename U>
inline void test_assert_eq(T a, U b, const char* name) {
    if (static_cast<long long>(a) == static_cast<long long>(b)) {
        test_pass(name);
    } else {
        test_fail(name, "value mismatch");
        uart_puts("  expected: "); uart_putnum(static_cast<int>(b)); 
        uart_puts(" got: "); uart_putnum(static_cast<int>(a)); 
        uart_puts("\r\n");
    }
}

}  // namespace

// =============================================================================
// Hardware Implementation for Renode
// =============================================================================

struct RenodeHw {
    // SysTick registers
    static inline volatile uint32_t* const SYST_CSR = reinterpret_cast<volatile uint32_t*>(0xE000E010);
    static inline volatile uint32_t* const SYST_RVR = reinterpret_cast<volatile uint32_t*>(0xE000E014);
    static inline volatile uint32_t* const SYST_CVR = reinterpret_cast<volatile uint32_t*>(0xE000E018);
    static inline volatile uint32_t* const SCB_ICSR = reinterpret_cast<volatile uint32_t*>(0xE000ED04);
    static inline volatile uint32_t* const DWT_CYCCNT = reinterpret_cast<volatile uint32_t*>(0xE0001004);
    static inline volatile uint32_t* const DWT_CTRL = reinterpret_cast<volatile uint32_t*>(0xE0001000);
    static inline volatile uint32_t* const CoreDebug_DEMCR = reinterpret_cast<volatile uint32_t*>(0xE000EDFC);
    
    // SysTick control bits
    static constexpr uint32_t SYST_ENABLE    = (1 << 0);
    static constexpr uint32_t SYST_TICKINT   = (1 << 1);
    static constexpr uint32_t SYST_CLKSOURCE = (1 << 2);
    
    static inline umi::usec os_ticks = 0;
    static inline umi::usec timer_target = 0;
    static inline bool systick_enabled = false;
    
    /// Initialize SysTick for 1ms ticks (Renode uses 72MHz)
    static void init_systick() {
        constexpr uint32_t CPU_FREQ = 72000000;   // 72 MHz (Renode default)
        constexpr uint32_t TICK_RATE_HZ = 1000;   // 1ms ticks
        constexpr uint32_t RELOAD = (CPU_FREQ / TICK_RATE_HZ) - 1;
        
        *SYST_RVR = RELOAD;
        *SYST_CVR = 0;
        *SYST_CSR = SYST_CLKSOURCE | SYST_TICKINT | SYST_ENABLE;
        systick_enabled = true;
    }
    
    static void set_timer_absolute(umi::usec target) { timer_target = target; }
    static umi::usec monotonic_time_usecs() { return os_ticks; }
    
    static void enter_critical() { asm volatile("cpsid i" ::: "memory"); }
    static void exit_critical() { asm volatile("cpsie i" ::: "memory"); }
    
    static void trigger_ipi(uint8_t) {}
    static uint8_t current_core() { return 0; }
    
    static void request_context_switch() {
        *SCB_ICSR = (1 << 28);  // Set PendSV
    }
    
    static void save_fpu() {}
    static void restore_fpu() {}
    static void mute_audio_dma() {}
    
    static void write_backup_ram(const void*, std::size_t) {}
    static void read_backup_ram(void*, std::size_t) {}
    
    static void configure_mpu_region(std::size_t, const void*, std::size_t, bool, bool) {}
    
    static void cache_clean(const void*, std::size_t) {}
    static void cache_invalidate(void*, std::size_t) {}
    static void cache_clean_invalidate(void*, std::size_t) {}
    
    static void system_reset() {
        volatile uint32_t* AIRCR = reinterpret_cast<volatile uint32_t*>(0xE000ED0C);
        *AIRCR = 0x05FA0004;
        while (1) {}
    }
    
    // SysTickが有効なら wfi で待機可能、そうでなければ即座にリターン
    static void enter_sleep() {
        if (systick_enabled) {
            asm volatile("wfi");
        }
        // SysTickが無効の場合は何もしない（テストがハングしないように）
    }
    [[noreturn]] static void start_first_task() { while(1) {} }
    
    static void watchdog_init(uint32_t) {}
    static void watchdog_feed() {}
    
    static uint32_t cycle_count() { return *DWT_CYCCNT; }
    static uint32_t cycles_per_usec() { return 168; }
};

using HW = umi::Hw<RenodeHw>;
using Kernel = umi::Kernel<8, 8, HW>;

// =============================================================================
// Test Suite
// =============================================================================

namespace {

Kernel kernel;

void test_task_creation() {
    uart_puts("\n--- Task Creation Tests ---\r\n");
    
    // Create task with name
    umi::TaskConfig cfg {
        .entry = nullptr,
        .arg = nullptr,
        .prio = umi::Priority::User,
        .name = "test_task"
    };
    
    auto tid = kernel.create_task(cfg);
    test_assert(tid.valid(), "create_task returns valid id", "");
    
    auto name = kernel.get_task_name(tid);
    test_assert(name != nullptr, "get_task_name not null", "");
    
    // Check priority
    auto prio = kernel.get_task_priority(tid);
    test_assert(prio == umi::Priority::User, "task priority correct", "");
    
    // Delete task
    bool deleted = kernel.delete_task(tid);
    test_assert(deleted, "delete_task succeeds", "");
    
    // Can't delete again
    deleted = kernel.delete_task(tid);
    test_assert(!deleted, "delete invalid task fails", "");
}

void test_notification() {
    uart_puts("\n--- Notification Tests ---\r\n");
    
    umi::TaskConfig cfg {
        .entry = nullptr,
        .prio = umi::Priority::User,
        .name = "notif_test"
    };
    
    auto tid = kernel.create_task(cfg);
    
    // Notify
    kernel.notify(tid, umi::Event::AudioReady);
    
    // Wait should return the bit
    auto bits = kernel.wait(tid, umi::Event::AudioReady);
    test_assert(bits == umi::Event::AudioReady, "notification delivered", "");
    
    // Wait again - should be cleared
    bits = kernel.wait(tid, umi::Event::AudioReady);
    test_assert(bits == 0, "notification cleared after wait", "");
    
    kernel.delete_task(tid);
}

void test_timer() {
    uart_puts("\n--- Timer Tests ---\r\n");
    
    static int timer_count = 0;
    auto callback = [](void* ctx) {
        (void)ctx;
        timer_count++;
    };
    
    umi::TimerCallback tcb { .fn = callback, .ctx = nullptr };
    
    bool ok = kernel.call_later(1000, tcb);
    test_assert(ok, "timer scheduled", "");
    
    // Tick partway - internal time advances
    kernel.tick(500);
    test_assert_eq(timer_count, 0, "timer not fired early");
    
    // Tick rest - timer should fire
    kernel.tick(600);
    test_assert_eq(timer_count, 1, "timer fired at deadline");
}

void test_spsc_queue() {
    uart_puts("\n--- SpscQueue Tests ---\r\n");
    
    umi::SpscQueue<int, 4> queue;
    
    // SpscQueue capacity is 3 (Capacity-1 usable slots)
    test_assert(!queue.try_pop().has_value(), "queue starts empty", "");
    test_assert(queue.has_space(), "queue has space initially", "");
    
    // Push items
    test_assert(queue.try_push(1), "push 1", "");
    test_assert(queue.try_push(2), "push 2", "");
    test_assert(queue.try_push(3), "push 3", "");
    test_assert(!queue.try_push(4), "push fails when full", "");
    
    // Pop items
    auto v = queue.try_pop();
    test_assert(v.has_value() && *v == 1, "pop 1", "");
    v = queue.try_pop();
    test_assert(v.has_value() && *v == 2, "pop 2", "");
    v = queue.try_pop();
    test_assert(v.has_value() && *v == 3, "pop 3", "");
    v = queue.try_pop();
    test_assert(!v.has_value(), "pop empty fails", "");
}

void test_expected() {
    uart_puts("\n--- Result/Expected Tests ---\r\n");
    
    // Test Ok - creates a Result with value
    umi::Result<int> success = umi::Ok(42);
    test_assert(success.has_value(), "Ok has value", "");
    test_assert_eq(*success, 42, "Ok value correct");
    
    // Test Err - creates a Result with error
    umi::Result<int> failure = umi::Err(umi::Error::OutOfTasks);
    test_assert(!failure.has_value(), "Err has no value", "");
    test_assert(failure.error() == umi::Error::OutOfTasks, "Err code correct", "");
}

void test_priority_scheduling() {
    uart_puts("\n--- Priority Scheduling Tests ---\r\n");
    
    // Create tasks with different priorities (created as Ready by default)
    umi::TaskConfig idle_cfg { .prio = umi::Priority::Idle, .name = "idle" };
    umi::TaskConfig user_cfg { .prio = umi::Priority::User, .name = "user" };
    umi::TaskConfig rt_cfg { .prio = umi::Priority::Realtime, .name = "realtime" };
    
    auto t_idle = kernel.create_task(idle_cfg);
    auto t_user = kernel.create_task(user_cfg);
    auto t_rt = kernel.create_task(rt_cfg);
    
    // get_next_task should select the highest priority task (realtime)
    auto next = kernel.get_next_task();
    test_assert(next.has_value() && next.value() == t_rt.value, 
                "realtime task selected first", "");
    
    // Clean up
    kernel.delete_task(t_idle);
    kernel.delete_task(t_user);
    kernel.delete_task(t_rt);
}

void test_stack_monitor() {
    uart_puts("\n--- Stack Monitor Tests ---\r\n");
    
    alignas(4) uint32_t stack[64];
    
    // Paint stack
    umi::StackMonitor<HW>::paint_stack(stack, sizeof(stack));
    
    // Initially should be 0% used (all magic)
    auto pct = umi::StackMonitor<HW>::usage_percent(stack, sizeof(stack));
    test_assert_eq(pct, 0, "stack initially 0% used");
    
    // Corrupt some of the stack (simulate usage)
    stack[60] = 0x12345678;
    stack[61] = 0x12345678;
    stack[62] = 0x12345678;
    stack[63] = 0x12345678;
    
    pct = umi::StackMonitor<HW>::usage_percent(stack, sizeof(stack));
    test_assert(pct > 0, "stack usage detected", "");
}

void test_for_each_task() {
    uart_puts("\n--- for_each_task Tests ---\r\n");
    
    // Create some tasks (created as Ready by default)
    umi::TaskConfig cfg1 { .prio = umi::Priority::User, .name = "task_a" };
    umi::TaskConfig cfg2 { .prio = umi::Priority::User, .name = "task_b" };
    
    auto t1 = kernel.create_task(cfg1);
    auto t2 = kernel.create_task(cfg2);
    // Note: create_task already sets State::Ready, no resume_task needed
    
    int count = 0;
    kernel.for_each_task([&](umi::TaskId id, const umi::TaskConfig& cfg, auto /*state*/) {
        (void)id; (void)cfg;
        count++;
    });
    
    test_assert(count >= 2, "for_each_task iterates tasks", "");
    
    kernel.delete_task(t1);
    kernel.delete_task(t2);
}

// Global for dynamic handler test
volatile uint32_t custom_handler_called = 0;

void test_custom_handler() {
    custom_handler_called = custom_handler_called + 1;
}

void test_vector_table() {
    uart_puts("\n--- Vector Table Tests ---\r\n");
    
    using VT = umi::port::arm::VectorTable<48>;
    VT vt;
    
    // Get SP from existing vector table
    auto sp = *reinterpret_cast<std::uint32_t*>(0x08000000);
    vt.init(sp, test_custom_handler);
    
    // Check alignment (compile-time computed)
    auto base = vt.base();
    test_assert(base != 0, "table base non-zero", "");
    test_assert((base & (VT::ALIGNMENT - 1)) == 0, "table properly aligned", "");
    
    // Get handler (initially default)
    auto h = vt.get(VT::Exc::SysTick);
    test_assert(h != nullptr, "handler not null", "");
    
    // Set custom handler and verify
    (void)vt.set(VT::Exc::PendSV, test_custom_handler);
    test_assert(vt.get(VT::Exc::PendSV) == test_custom_handler, "handler set correctly", "");
    
    // Test IRQ handler
    vt.set_irq(38, test_custom_handler);  // USART2
    test_assert(vt.get_irq(38) == test_custom_handler, "IRQ handler set", "");
    
    // Restore original VTOR for rest of tests
    umi::port::arm::SCB::set_vtor(0x08000000);
}

}  // namespace

// =============================================================================
// Main Entry Point (C++ linkage is fine; Reset_Handler calls directly)
// =============================================================================

int main() {
    uart_init();
    
    // Initialize SysTick for proper timing and wfi wake-up
    RenodeHw::init_systick();
    
    uart_puts("\r\n");
    uart_puts("========================================\r\n");
    uart_puts("  UMI-OS Comprehensive Renode Tests\r\n");
    uart_puts("========================================\r\n");
    
    // Run all test suites
    test_task_creation();
    test_notification();
    test_timer();
    test_spsc_queue();
    test_expected();
    test_priority_scheduling();
    test_stack_monitor();
    test_for_each_task();
    test_vector_table();
    
    // Summary
    uart_puts("\r\n========================================\r\n");
    uart_puts("  Test Summary\r\n");
    uart_puts("========================================\r\n");
    uart_puts("Total:  "); uart_putnum(test_count); uart_puts("\r\n");
    uart_puts("Passed: "); uart_putnum(pass_count); uart_puts("\r\n");
    uart_puts("Failed: "); uart_putnum(fail_count); uart_puts("\r\n");
    
    if (fail_count == 0) {
        uart_puts("\r\n*** ALL TESTS PASSED ***\r\n");
    } else {
        uart_puts("\r\n*** SOME TESTS FAILED ***\r\n");
    }
    
    uart_puts("TEST_COMPLETE\r\n");  // Marker for Robot Framework
    
    // Signal test completion to Renode via magic address write
    // Renode script sets a hook on this address to trigger exit
    volatile uint32_t* test_exit = reinterpret_cast<volatile uint32_t*>(0xE0000000);
    *test_exit = (fail_count == 0) ? 0 : 1;  // 0 = success, 1 = failure
    
    while (1) {
        asm volatile("wfi");
    }
}

// =============================================================================
// Vector Table and Exception Handlers for Cortex-M4
// =============================================================================

extern "C" {
    extern uint32_t _estack;
    
    void Reset_Handler();
    void NMI_Handler();
    void HardFault_Handler();
    void MemManage_Handler();
    void BusFault_Handler();
    void UsageFault_Handler();
    void SVC_Handler();
    void DebugMon_Handler();
    void PendSV_Handler();
    void SysTick_Handler();
    void Default_Handler();
    
    // Weak aliases - can be overridden
    void NMI_Handler()        { while(1); }
    void HardFault_Handler()  { while(1); }
    void MemManage_Handler()  { while(1); }
    void BusFault_Handler()   { while(1); }
    void UsageFault_Handler() { while(1); }
    void SVC_Handler()        { /* SVC not used in tests */ }
    void DebugMon_Handler()   { }
    void PendSV_Handler()     { /* Context switch - minimal for tests */ }
    void SysTick_Handler()    { RenodeHw::os_ticks += 1000; }  // 1ms tick
    void Default_Handler()    { while(1); }
    
    void Reset_Handler() { 
        main(); 
    }
    
    // Full Cortex-M4 vector table (first 16 entries are system exceptions)
    #ifdef __arm__
    __attribute__((section(".isr_vector"), used))
    #endif
    const void* vector_table[48] = {
        &_estack,                              // 0: Initial SP
        reinterpret_cast<void*>(Reset_Handler),       // 1: Reset
        reinterpret_cast<void*>(NMI_Handler),         // 2: NMI
        reinterpret_cast<void*>(HardFault_Handler),   // 3: HardFault
        reinterpret_cast<void*>(MemManage_Handler),   // 4: MemManage
        reinterpret_cast<void*>(BusFault_Handler),    // 5: BusFault
        reinterpret_cast<void*>(UsageFault_Handler),  // 6: UsageFault
        nullptr, nullptr, nullptr, nullptr,           // 7-10: Reserved
        reinterpret_cast<void*>(SVC_Handler),         // 11: SVCall
        reinterpret_cast<void*>(DebugMon_Handler),    // 12: DebugMon
        nullptr,                                      // 13: Reserved
        reinterpret_cast<void*>(PendSV_Handler),      // 14: PendSV
        reinterpret_cast<void*>(SysTick_Handler),     // 15: SysTick
        // External interrupts (16+)
        reinterpret_cast<void*>(Default_Handler),     // 16: WWDG
        reinterpret_cast<void*>(Default_Handler),     // 17: PVD
        reinterpret_cast<void*>(Default_Handler),     // 18: TAMP_STAMP
        reinterpret_cast<void*>(Default_Handler),     // 19: RTC_WKUP
        reinterpret_cast<void*>(Default_Handler),     // 20: FLASH
        reinterpret_cast<void*>(Default_Handler),     // 21: RCC
        reinterpret_cast<void*>(Default_Handler),     // 22-31
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),     // 31
        reinterpret_cast<void*>(Default_Handler),     // 32-47
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),     // 38: USART2
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
        reinterpret_cast<void*>(Default_Handler),
    };
}
