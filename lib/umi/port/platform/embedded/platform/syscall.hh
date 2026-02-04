// SPDX-License-Identifier: MIT
// UMI-OS Syscall Implementation for Cortex-M (SVC-based)
#pragma once

#include <cstdint>
#include <umi/kernel/syscall/syscall_numbers.hh>

namespace umi::platform {

// ============================================================================
// Generic SVC caller with runtime syscall number
// ============================================================================
// SVC immediate must be compile-time, so we use a fixed SVC #0 and
// pass the actual syscall number in r12. The SVC handler extracts it.

[[gnu::always_inline]] inline uint32_t syscall0(uint8_t num) {
    register uint32_t r0 asm("r0");
    register uint32_t r12 asm("r12") = num;
    asm volatile("svc #0" : "=r"(r0) : "r"(r12) : "memory");
    return r0;
}

[[gnu::always_inline]] inline uint32_t syscall1(uint8_t num, uint32_t arg0) {
    register uint32_t r0 asm("r0") = arg0;
    register uint32_t r12 asm("r12") = num;
    asm volatile("svc #0" : "+r"(r0) : "r"(r12) : "memory");
    return r0;
}

[[gnu::always_inline]] inline uint32_t syscall2(uint8_t num, uint32_t arg0, uint32_t arg1) {
    register uint32_t r0 asm("r0") = arg0;
    register uint32_t r1 asm("r1") = arg1;
    register uint32_t r12 asm("r12") = num;
    asm volatile("svc #0" : "+r"(r0) : "r"(r12), "r"(r1) : "memory");
    return r0;
}

[[gnu::always_inline]] inline uint32_t syscall3(uint8_t num, uint32_t arg0, uint32_t arg1, uint32_t arg2) {
    register uint32_t r0 asm("r0") = arg0;
    register uint32_t r1 asm("r1") = arg1;
    register uint32_t r2 asm("r2") = arg2;
    register uint32_t r12 asm("r12") = num;
    asm volatile("svc #0" : "+r"(r0) : "r"(r12), "r"(r1), "r"(r2) : "memory");
    return r0;
}

[[gnu::always_inline]] inline uint32_t
syscall4(uint8_t num, uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    register uint32_t r0 asm("r0") = arg0;
    register uint32_t r1 asm("r1") = arg1;
    register uint32_t r2 asm("r2") = arg2;
    register uint32_t r3 asm("r3") = arg3;
    register uint32_t r12 asm("r12") = num;
    asm volatile("svc #0" : "+r"(r0) : "r"(r12), "r"(r1), "r"(r2), "r"(r3) : "memory");
    return r0;
}

// ============================================================================
// Type-safe Syscall API
// ============================================================================
// Syscall numbers from kernel/syscall/syscall_numbers.hh

namespace nr = umi::syscall;

/// Terminate application
[[noreturn]] inline void sys_exit(int32_t code) {
    syscall1(nr::sys_exit, static_cast<uint32_t>(code));
    __builtin_unreachable();
}

/// Yield to scheduler
inline void sys_yield() {
    syscall0(nr::sys_yield);
}

/// Wait for events with optional timeout
/// @param mask Event mask (OR of event bits)
/// @param timeout_us Timeout in microseconds (0 = indefinite)
/// @return Events that occurred (bitmask)
inline uint32_t sys_wait_event(uint32_t mask, uint32_t timeout_us = 0) {
    return syscall2(nr::sys_wait_event, mask, timeout_us);
}

/// Get system time in microseconds (64-bit)
inline uint64_t sys_get_time() {
    register uint32_t r0 asm("r0");
    register uint32_t r1 asm("r1");
    register uint32_t r12 asm("r12") = nr::sys_get_time;
    asm volatile("svc #0" : "=r"(r0), "=r"(r1) : "r"(r12) : "memory");
    return (static_cast<uint64_t>(r1) << 32) | r0;
}

/// Get shared memory region pointer
/// @param region_id Region identifier
inline void* sys_get_shared(uint32_t region_id = 0) {
    uint32_t result = syscall1(nr::sys_get_shared, region_id);
    return reinterpret_cast<void*>(result);
}

/// Register audio processor
/// @param instance Processor instance pointer
/// @param func Process function pointer (optional, for C-style registration)
inline int32_t sys_register_proc(uint32_t instance, uint32_t func = 0) {
    return static_cast<int32_t>(syscall2(nr::sys_register_proc, instance, func));
}

/// Unregister audio processor
inline int32_t sys_unregister_proc(uint32_t instance) {
    return static_cast<int32_t>(syscall1(nr::sys_unregister_proc, instance));
}

} // namespace umi::platform
