// SPDX-License-Identifier: MIT
/// @file
/// @brief Minimal newlib syscall and stdio bridge for STM32F4/Renode.
/// @note stdout/stderr are routed to platform UART output.

#include <sys/stat.h>

#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

#include "platform.hh"

extern "C" {

extern std::uint8_t _end;    // End of .bss (heap start)
extern std::uint8_t _estack; // Top of stack (heap limit)

static std::uint8_t* g_heap_ptr = &_end;

/// @brief Grow heap region for dynamic allocation.
void* _sbrk(ptrdiff_t incr) {
    std::uint8_t* prev = g_heap_ptr;
    std::uint8_t* next = g_heap_ptr + incr;

    const auto stack_limit = reinterpret_cast<std::uintptr_t>(&_estack) - 1024u;
    if (reinterpret_cast<std::uintptr_t>(next) >= stack_limit) {
        errno = ENOMEM;
        return reinterpret_cast<void*>(-1);
    }

    g_heap_ptr = next;
    return prev;
}

/// @brief Write a byte buffer to stdout/stderr UART sink.
static int write_buf(int fd, const char* buf, int len) {
    if (fd != 1 && fd != 2) {
        return len;
    }

    using Output = umi::port::Platform::Output;
    for (int i = 0; i < len; ++i) {
        const char c = buf[i];
        if (c == '\n') {
            Output::putc('\r');
        }
        Output::putc(c);
    }
    return len;
}

/// @brief Syscall write implementation.
int _write(int fd, const char* buf, int len) {
    return write_buf(fd, buf, len);
}

/// @brief POSIX write alias for libraries using `::write()` directly.
int write(int fd, const void* buf, unsigned len) {
    return write_buf(fd, static_cast<const char*>(buf), static_cast<int>(len));
}

/// @brief Write one character to the selected descriptor sink.
static int write_char(int fd, char c) {
    return write_buf(fd, &c, 1);
}

/// @brief Write a null-terminated string.
static int write_cstr(int fd, const char* s) {
    const char* text = s ? s : "(null)";
    int len = 0;
    while (text[len] != '\0') {
        ++len;
    }
    return write_buf(fd, text, len);
}

/// @brief Write an unsigned integer with base formatting.
static int write_unsigned(int fd, unsigned long long value, unsigned base, bool upper_case) {
    static constexpr char digits_lower[] = "0123456789abcdef";
    static constexpr char digits_upper[] = "0123456789ABCDEF";
    const char* digits = upper_case ? digits_upper : digits_lower;

    char buf[32];
    int i = 0;
    do {
        const unsigned digit = static_cast<unsigned>(value % base);
        buf[i++] = digits[digit];
        value /= base;
    } while (value > 0);

    int written = 0;
    while (i-- > 0) {
        written += write_char(fd, buf[i]);
    }
    return written;
}

/// @brief Write a signed integer in decimal.
static int write_signed(int fd, long long value) {
    int written = 0;
    if (value < 0) {
        written += write_char(fd, '-');
        const auto magnitude = static_cast<unsigned long long>(-(value + 1)) + 1ULL;
        written += write_unsigned(fd, magnitude, 10, false);
        return written;
    }
    return written + write_unsigned(fd, static_cast<unsigned long long>(value), 10, false);
}

/// @brief Minimal `vprintf` implementation for embedded target logs.
int vprintf(const char* format, va_list args) {
    int written = 0;
    for (const char* p = format; *p != '\0'; ++p) {
        if (*p != '%') {
            written += write_char(1, *p);
            continue;
        }

        ++p;
        if (*p == '\0') {
            break;
        }
        if (*p == '%') {
            written += write_char(1, '%');
            continue;
        }

        enum class Length { none, l, ll };
        Length length = Length::none;
        if (*p == 'l') {
            ++p;
            if (*p == 'l') {
                length = Length::ll;
                ++p;
            } else {
                length = Length::l;
            }
        }

        if (*p == '\0') {
            break;
        }

        switch (*p) {
        case 'c':
            written += write_char(1, static_cast<char>(va_arg(args, int)));
            break;
        case 's':
            written += write_cstr(1, va_arg(args, const char*));
            break;
        case 'd':
        case 'i':
            if (length == Length::ll) {
                written += write_signed(1, va_arg(args, long long));
            } else if (length == Length::l) {
                written += write_signed(1, va_arg(args, long));
            } else {
                written += write_signed(1, va_arg(args, int));
            }
            break;
        case 'u':
            if (length == Length::ll) {
                written += write_unsigned(1, va_arg(args, unsigned long long), 10, false);
            } else if (length == Length::l) {
                written += write_unsigned(1, va_arg(args, unsigned long), 10, false);
            } else {
                written += write_unsigned(1, va_arg(args, unsigned int), 10, false);
            }
            break;
        case 'x':
            if (length == Length::ll) {
                written += write_unsigned(1, va_arg(args, unsigned long long), 16, false);
            } else if (length == Length::l) {
                written += write_unsigned(1, va_arg(args, unsigned long), 16, false);
            } else {
                written += write_unsigned(1, va_arg(args, unsigned int), 16, false);
            }
            break;
        case 'X':
            if (length == Length::ll) {
                written += write_unsigned(1, va_arg(args, unsigned long long), 16, true);
            } else if (length == Length::l) {
                written += write_unsigned(1, va_arg(args, unsigned long), 16, true);
            } else {
                written += write_unsigned(1, va_arg(args, unsigned int), 16, true);
            }
            break;
        case 'p':
            written += write_cstr(1, "0x");
            written += write_unsigned(
                1, static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(va_arg(args, void*))), 16, false);
            break;
        default:
            written += write_char(1, '%');
            written += write_char(1, *p);
            break;
        }
    }
    return written;
}

/// @brief Minimal `printf` implementation backed by `vprintf`.
int printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int written = vprintf(format, args);
    va_end(args);
    return written;
}

/// @brief Output one character to stdout.
int putchar(int c) {
    write_char(1, static_cast<char>(c));
    return c;
}

/// @brief Output one line to stdout.
int puts(const char* s) {
    int written = write_cstr(1, s);
    written += write_char(1, '\n');
    return written;
}

/// @brief Stub read syscall (not supported).
int _read(int fd, char* buf, int len) {
    (void)fd;
    (void)buf;
    (void)len;
    return 0;
}

/// @brief Stub close syscall.
int _close(int fd) {
    (void)fd;
    return -1;
}

/// @brief Report character-device mode for stdio descriptors.
int _fstat(int fd, struct stat* st) {
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}

/// @brief Return whether descriptor is a TTY-like endpoint.
int _isatty(int fd) {
    return (fd < 3) ? 1 : 0;
}

/// @brief Stub seek syscall.
int _lseek(int fd, int offset, int whence) {
    (void)fd;
    (void)offset;
    (void)whence;
    return 0;
}

/// @brief Return synthetic process identifier.
int _getpid() {
    return 1;
}

/// @brief Stub signal syscall.
int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    return -1;
}

/// @brief Terminate process by parking CPU forever.
void _exit(int status) {
    (void)status;
    while (true) {
        asm volatile("wfi");
    }
}

} // extern "C"
