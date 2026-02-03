// SPDX-License-Identifier: MIT
// UMI-OS Application Syscall Interface
// Low-level syscall wrappers for ARM Cortex-M

#pragma once

#include "../core/syscall_nr.hh"
#include "../core/fs_types.hh"

namespace umi::syscall {

// ============================================================================
// Low-level Syscall Invocation
// ============================================================================

#if defined(__ARM_ARCH)

[[gnu::always_inline]] inline int32_t call(uint32_t nr, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0,
                                           uint32_t a3 = 0) noexcept {
    register uint32_t r0 __asm__("r0") = a0;
    register uint32_t r1 __asm__("r1") = a1;
    register uint32_t r2 __asm__("r2") = a2;
    register uint32_t r3 __asm__("r3") = a3;
    register uint32_t r12 __asm__("r12") = nr;

    __asm__ volatile("svc #0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r12) : "memory");

    return static_cast<int32_t>(r0);
}

#else

// Host/simulation stub
inline int32_t call(uint32_t nr, uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) noexcept {
    (void)nr;
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    return 0;
}

#endif

// ============================================================================
// Typed Syscall Wrappers — Process / Time / Config / IO
// ============================================================================

[[noreturn]] inline void exit(int code) noexcept {
    call(nr::exit, static_cast<uint32_t>(code));
    while (true) {
        __asm__ volatile("");
    }
}

[[noreturn]] inline void panic(const char* msg) noexcept {
    (void)msg;
    exit(-1);
}

inline void yield() noexcept {
    call(nr::yield);
}

inline uint32_t wait_event(uint32_t mask, uint32_t timeout_usec = 0) noexcept {
    return static_cast<uint32_t>(call(nr::wait_event, mask, timeout_usec));
}

inline void sleep_usec(uint32_t usec) noexcept {
    call(nr::sleep, usec);
}

inline uint64_t get_time_usec() noexcept {
#if defined(__ARM_ARCH)
    register uint32_t r0 __asm__("r0");
    register uint32_t r1 __asm__("r1");
    register uint32_t r12 __asm__("r12") = nr::get_time;
    __asm__ volatile("svc #0" : "=r"(r0), "=r"(r1) : "r"(r12) : "memory");
    return (static_cast<uint64_t>(r1) << 32) | r0;
#else
    return 0;
#endif
}

inline void* get_shared() noexcept {
    return reinterpret_cast<void*>(call(nr::get_shared));
}

inline void log(const char* msg) noexcept {
    call(nr::log, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(msg)));
}

inline int32_t set_app_config(const void* config) noexcept {
    return call(nr::set_app_config, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(config)));
}

inline int32_t send_param_request(uint32_t param_id, float value) noexcept {
    uint32_t value_bits;
    __builtin_memcpy(&value_bits, &value, sizeof(value_bits));
    return call(nr::send_param_request, param_id, value_bits);
}

// ============================================================================
// Typed Syscall Wrappers — Filesystem (async, non-blocking)
// ============================================================================
// All FS syscalls (except fs::result) enqueue a request to StorageService
// and return immediately (0 = accepted, -EBUSY = previous request pending).
// Result is delivered via event::fs and retrieved with fs::result().

namespace fs {

inline int32_t open(const char* path, OpenFlags flags) noexcept {
    return call(nr::file_open,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path)),
                static_cast<uint32_t>(flags));
}

inline int32_t read(int fd, void* buf, uint32_t len) noexcept {
    return call(nr::file_read,
                static_cast<uint32_t>(fd),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf)),
                len);
}

inline int32_t write(int fd, const void* buf, uint32_t len) noexcept {
    return call(nr::file_write,
                static_cast<uint32_t>(fd),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf)),
                len);
}

inline int32_t close(int fd) noexcept {
    return call(nr::file_close, static_cast<uint32_t>(fd));
}

inline int32_t seek(int fd, int32_t offset, Whence whence) noexcept {
    return call(nr::file_seek,
                static_cast<uint32_t>(fd),
                static_cast<uint32_t>(offset),
                static_cast<uint32_t>(whence));
}

inline int32_t tell(int fd) noexcept {
    return call(nr::file_tell, static_cast<uint32_t>(fd));
}

inline int32_t size(int fd) noexcept {
    return call(nr::file_size, static_cast<uint32_t>(fd));
}

inline int32_t truncate(int fd, uint32_t new_size) noexcept {
    return call(nr::file_truncate, static_cast<uint32_t>(fd), new_size);
}

inline int32_t sync(int fd) noexcept {
    return call(nr::file_sync, static_cast<uint32_t>(fd));
}

inline int32_t dir_open(const char* path) noexcept {
    return call(nr::dir_open, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path)));
}

inline int32_t dir_read(int dirfd, FsInfo* info) noexcept {
    return call(nr::dir_read,
                static_cast<uint32_t>(dirfd),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info)));
}

inline int32_t dir_close(int dirfd) noexcept {
    return call(nr::dir_close, static_cast<uint32_t>(dirfd));
}

inline int32_t dir_seek(int dirfd, uint32_t off) noexcept {
    return call(nr::dir_seek, static_cast<uint32_t>(dirfd), off);
}

inline int32_t dir_tell(int dirfd) noexcept {
    return call(nr::dir_tell, static_cast<uint32_t>(dirfd));
}

inline int32_t stat(const char* path, FsInfo* info) noexcept {
    return call(nr::stat,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path)),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info)));
}

inline int32_t fstat(int fd, FsInfo* info) noexcept {
    return call(nr::fstat,
                static_cast<uint32_t>(fd),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info)));
}

inline int32_t mkdir(const char* path) noexcept {
    return call(nr::mkdir, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path)));
}

inline int32_t remove(const char* path) noexcept {
    return call(nr::remove, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path)));
}

inline int32_t rename(const char* oldpath, const char* newpath) noexcept {
    return call(nr::rename,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(oldpath)),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(newpath)));
}

/// Get custom attribute. type in low 8 bits of r1, len in bits [8..31].
inline int32_t getattr(const char* path, uint8_t type, void* buf, uint32_t len) noexcept {
    return call(nr::getattr,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path)),
                static_cast<uint32_t>(type) | (len << 8),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf)));
}

/// Set custom attribute. type in low 8 bits of r1, len in bits [8..31].
inline int32_t setattr(const char* path, uint8_t type, const void* buf, uint32_t len) noexcept {
    return call(nr::setattr,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path)),
                static_cast<uint32_t>(type) | (len << 8),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buf)));
}

inline int32_t removeattr(const char* path, uint8_t type) noexcept {
    return call(nr::removeattr,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path)),
                static_cast<uint32_t>(type));
}

inline int32_t fs_stat(const char* path, FsStatInfo* info) noexcept {
    return call(nr::fs_stat,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(path)),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info)));
}

/// Retrieve result of last FS operation.
/// Call after receiving event::fs. Returns -EAGAIN (-11) if not ready.
/// Clears result slot, allowing next FS request.
inline int32_t result() noexcept {
    return call(nr::fs_result);
}

/// Synchronous helper: enqueue request, wait for completion, return result.
inline int32_t sync_call(auto request_fn) noexcept {
    request_fn();
    wait_event(event::fs);
    return result();
}

} // namespace fs

// ============================================================================
// Coroutine Scheduler Adapters
// ============================================================================

namespace coro_adapter {

inline uint32_t wait(uint32_t mask, uint64_t timeout_us) noexcept {
    return wait_event(mask, static_cast<uint32_t>(timeout_us));
}

inline uint64_t get_time() noexcept {
    return static_cast<uint64_t>(get_time_usec());
}

} // namespace coro_adapter

} // namespace umi::syscall
