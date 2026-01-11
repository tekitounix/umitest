// SPDX-License-Identifier: MIT
// UMI-OS STM32F4 Newlib Syscalls (Minimal)

#include <cstddef>
#include <cstdint>
#include <sys/stat.h>
#include <errno.h>

extern "C" {

// Linker symbols for heap
extern std::uint8_t _end;      // End of .bss (heap start)
extern std::uint8_t _estack;   // Stack top (heap limit)

static std::uint8_t* heap_ptr = &_end;

// =============================================================================
// Required Syscalls
// =============================================================================

void* _sbrk(ptrdiff_t incr) {
    auto* prev = heap_ptr;
    auto* next = heap_ptr + incr;
    
    // Check against stack (leave 1KB margin)
    // Use reinterpret_cast to avoid array-bounds warning
    auto stack_limit = reinterpret_cast<std::uintptr_t>(&_estack) - 1024;
    if (reinterpret_cast<std::uintptr_t>(next) >= stack_limit) {
        errno = ENOMEM;
        return reinterpret_cast<void*>(-1);
    }
    
    heap_ptr = next;
    return prev;
}

int _write(int fd, const char* buf, int len) {
    (void)fd; (void)buf;
    return len;  // Stub: discard output
}

int _read(int fd, char* buf, int len) {
    (void)fd; (void)buf; (void)len;
    return 0;  // EOF
}

int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, struct stat* st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { return (fd < 3) ? 1 : 0; }
int _lseek(int fd, int offset, int whence) { (void)fd; (void)offset; (void)whence; return 0; }
int _getpid() { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }

void _exit(int status) { (void)status; while(1); }

} // extern "C"
