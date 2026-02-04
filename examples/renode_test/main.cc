// SPDX-License-Identifier: MIT
// UMI-OS Renode Test - Kernel Main

#include <cstdint>

// Platform-specific includes (resolved by build system)
#include <platform/privilege.hh>
#include <platform/protection.hh>
#include <platform/syscall.hh>

// Drivers
#include <arch/svc_handler.hh>
#include <common/systick_driver.hh>
#include <common/uart_driver.hh>

// Force svc_dispatch symbol emission (called from startup.cc asm)
[[gnu::used]] static auto* const svc_dispatch_ptr = &umi::kernel::svc_dispatch;

// Linker symbols
extern uint32_t _user_ram_start;
extern uint32_t _estack;

// ============================================================================
// Kernel State
// ============================================================================

namespace umi::kernel {

// Syscall implementation state
namespace impl {
uint64_t system_time_us = 0;
uint32_t current_task_id = 1;
} // namespace impl

// Kernel state pointer (required by svc_handler.hh)
KernelState* g_kernel = nullptr;

// FS syscall hooks (optional)
bool (*g_fs_enqueue)(uint8_t, uint32_t, uint32_t, uint32_t, uint32_t) = nullptr;
int32_t (*g_fs_consume_result)() = nullptr;

} // namespace umi::kernel

// ============================================================================
// User Application (runs in unprivileged mode)
// ============================================================================

namespace app {

/// Application main loop
[[noreturn]] void main_loop() {
    while (true) {
        // Yield to other tasks (syscall)
        umi::platform::sys_yield();
    }
}

} // namespace app

// ============================================================================
// SysTick Callback
// ============================================================================

void on_systick(void*) {
    // Update system time
    umi::kernel::impl::system_time_us += 1000; // 1ms tick
}

// ============================================================================
// Kernel Main
// ============================================================================

extern "C" int main() {
    using namespace umi;

    // Initialize UART driver first
    driver::UartConfig uart_cfg = {.baud_rate = 115200, .data_bits = 8, .stop_bits = 1, .parity = 0};
    driver::uart::init(&uart_cfg);

    // Print banner
    driver::uart::puts("\n");
    driver::uart::puts("================================\n");
    driver::uart::puts("  UMI-OS v0.2.0 (Renode Test)\n");
    driver::uart::puts("================================\n");

    // Initialize MPU
    if (mpu::is_available()) {
        driver::uart::puts("MPU available, ");
        mpu::init_umi_regions();
        driver::uart::puts("regions configured\n");
    } else {
        driver::uart::puts("MPU not available\n");
    }

    // Initialize SysTick driver (1ms ticks)
    driver::TimerConfig timer_cfg = {.tick_hz = 1000};
    driver::systick::init(&timer_cfg);
    driver::systick::set_callback(on_systick, nullptr);
    driver::uart::puts("SysTick initialized (1ms)\n");

    // Set up vector table for SVC handler
    // (SVC_Handler is already in vector table from startup.cc)

    driver::uart::puts("Kernel ready, entering user mode\n");
    driver::uart::puts("--------------------------------\n");

    // Prepare user stack
    // User stack starts at _user_ram_start, grows down from top
    uint32_t user_stack_top = reinterpret_cast<uint32_t>(&_estack) - 0x1000;

    // Enter user mode and start application
    privilege::enter_user_mode(user_stack_top, app::main_loop);

    // Never reached
    return 0;
}
