// SPDX-License-Identifier: MIT
// UMI-OS Application C Runtime Startup (crt0)
// Entry point for .umia binaries

#include <cstdint>

#include "syscall.hh"

// ============================================================================
// Linker-provided symbols
// ============================================================================

extern "C" {

// Initialized data section (copy from flash to RAM)
extern uint32_t _sidata; // Start of .data in flash
extern uint32_t _sdata;  // Start of .data in RAM
extern uint32_t _edata;  // End of .data in RAM

// Uninitialized data section (zero-fill)
extern uint32_t _sbss; // Start of .bss
extern uint32_t _ebss; // End of .bss

// Heap/Stack symbols (from linker)
extern uint8_t _sheap;  // Heap start (APP_STACK origin)
extern uint8_t _eheap;  // Heap end (APP_STACK end)
extern uint8_t _estack; // Stack top (APP_STACK end)

// Global constructors/destructors
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
extern void (*__fini_array_start[])(void);
extern void (*__fini_array_end[])(void);

// User main function
int main();
}

// ============================================================================
// Startup Code
// ============================================================================

extern "C" {

/// Application entry point (called by kernel)
///
/// This function:
/// 1. Initializes .data and .bss sections
/// 2. Calls global constructors
/// 3. Calls main()
/// 4. Returns to kernel (does NOT call exit - allows kernel to continue)
void _start() {
    // Initialize .data section (copy from flash to RAM)
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    // Initialize .bss section (zero-fill)
    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    // Call global constructors
    for (auto fn = __init_array_start; fn < __init_array_end; ++fn) {
        (*fn)();
    }

    // Call main - typically registers processor and returns
    // After main() returns, control returns to kernel normally
    // (Yield syscall is no longer needed - kernel continues in privileged mode)
    main();
}

/// Minimal abort handler
[[noreturn]] void abort() {
    umi::syscall::panic("abort() called");
}

/// Minimal exit handler (called by C library)
[[noreturn]] void _exit(int code) {
    umi::syscall::exit(code);
}

/// Pure virtual function call handler
[[noreturn]] void __cxa_pure_virtual() {
    umi::syscall::panic("pure virtual function called");
}

/// Deleted virtual function call handler
[[noreturn]] void __cxa_deleted_virtual() {
    umi::syscall::panic("deleted virtual function called");
}

/// Guard acquire (for static local variables)
/// Returns 1 if initialization is needed, 0 if already done
int __cxa_guard_acquire(int64_t* guard) {
    // Simple implementation - assumes single-threaded initialization
    if (*guard == 0) {
        *guard = 1;
        return 1; // Need to initialize
    }
    return 0; // Already initialized
}

/// Guard release (mark initialization complete)
void __cxa_guard_release(int64_t* guard) {
    *guard = 2; // Mark as fully initialized
}

/// Guard abort (initialization failed)
void __cxa_guard_abort(int64_t* guard) {
    *guard = 0; // Reset to uninitialized
}

} // extern "C"

// ============================================================================
// Minimal Allocator for Coroutines
// ============================================================================

// Simple bump allocator for coroutine frames
// Uses APP_STACK region and checks for heap/stack collision
namespace {
static inline uint8_t* heap_ptr = nullptr;

static inline uintptr_t current_sp() {
#if defined(__ARM_ARCH)
    uintptr_t sp;
    __asm__ volatile("mrs %0, psp" : "=r"(sp)::"memory");
    if (sp == 0 || sp < reinterpret_cast<uintptr_t>(&_sheap) || sp > reinterpret_cast<uintptr_t>(&_estack)) {
        sp = reinterpret_cast<uintptr_t>(&_estack);
    }
    return sp;
#else
    return reinterpret_cast<uintptr_t>(&_estack);
#endif
}
} // namespace

void* operator new(decltype(sizeof(0)) size) {
    // Align to 8 bytes
    size = (size + 7) & ~static_cast<decltype(size)>(7);

    if (heap_ptr == nullptr) {
        heap_ptr = &_sheap;
    }

    uint8_t* next = heap_ptr + size;
    uintptr_t sp = current_sp();

    // Heap/stack collision check
    if (next > &_eheap || next >= reinterpret_cast<uint8_t*>(sp)) {
        umi::syscall::panic("out of heap memory");
    }

    void* ptr = heap_ptr;
    heap_ptr = next;
    return ptr;
}

void operator delete(void* ptr, decltype(sizeof(0)) /*size*/) noexcept {
    // Bump allocator doesn't support free
    (void)ptr;
}

void operator delete(void* ptr) noexcept {
    (void)ptr;
}
