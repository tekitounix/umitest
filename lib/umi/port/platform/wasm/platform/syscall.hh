// SPDX-License-Identifier: MIT
// UMI-OS Syscall Implementation for WASM (direct call)
#pragma once

#include <cstdint>
#include <umi/kernel/syscall/syscall_numbers.hh>

// Forward declarations - implemented by WASM kernel
namespace umi::kernel {
void kernel_exit(int32_t code);
void kernel_yield();
uint32_t kernel_wait_event(uint32_t mask, uint32_t timeout_us);
uint64_t kernel_get_time();
void* kernel_get_shared(uint32_t region_id);
int32_t kernel_register_proc(uint32_t instance, uint32_t func);
int32_t kernel_unregister_proc(uint32_t instance);
} // namespace umi::kernel

namespace umi::platform {

// ============================================================================
// WASM Syscall Implementation (direct function calls)
// ============================================================================
// In WASM, there's no hardware privilege separation. Syscalls are
// implemented as direct function calls to the kernel module.
// The WASM sandbox provides isolation.

[[noreturn]] inline void sys_exit(int32_t code) {
    kernel::kernel_exit(code);
    __builtin_unreachable();
}

inline void sys_yield() {
    kernel::kernel_yield();
}

inline uint32_t sys_wait_event(uint32_t mask, uint32_t timeout_us = 0) {
    return kernel::kernel_wait_event(mask, timeout_us);
}

inline uint64_t sys_get_time() {
    return kernel::kernel_get_time();
}

inline void* sys_get_shared(uint32_t region_id = 0) {
    return kernel::kernel_get_shared(region_id);
}

inline int32_t sys_register_proc(uint32_t instance, uint32_t func = 0) {
    return kernel::kernel_register_proc(instance, func);
}

inline int32_t sys_unregister_proc(uint32_t instance) {
    return kernel::kernel_unregister_proc(instance);
}

} // namespace umi::platform
