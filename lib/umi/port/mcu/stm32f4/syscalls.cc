// SPDX-License-Identifier: MIT
// UMI-OS STM32F4 Newlib/Picolibc Syscalls (Minimal)

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>

extern "C" {

// Linker symbols for heap
extern uint8_t _end;    // End of .bss (heap start)
extern uint8_t _estack; // Stack top (heap limit)

static uint8_t* heap_ptr = &_end;

// =============================================================================
// Required Syscalls
// =============================================================================

void* _sbrk(ptrdiff_t incr) {
    uint8_t* prev = heap_ptr;
    uint8_t* next = heap_ptr + incr;

    // Check against stack (leave 1KB margin)
    uintptr_t stack_limit = (uintptr_t)&_estack - 1024;
    if ((uintptr_t)next >= stack_limit) {
        errno = ENOMEM;
        return (void*)-1;
    }

    heap_ptr = next;
    return prev;
}

// USART2 registers (APB1: 0x40004400)
#define USART2_BASE  0x40004400UL
#define USART2_SR    (USART2_BASE + 0x00)
#define USART2_DR    (USART2_BASE + 0x04)
#define USART2_BRR   (USART2_BASE + 0x08)
#define USART2_CR1   (USART2_BASE + 0x0C)

#define USART_SR_TXE (1 << 7)  // Transmit data register empty
#define USART_CR1_UE (1 << 13) // USART enable
#define USART_CR1_TE (1 << 3)  // Transmitter enable

static volatile int uart_initialized = 0;

static void uart_init_once(void) {
    if (uart_initialized)
        return;

    // Enable USART2 clock (RCC APB1ENR bit 17)
    volatile uint32_t* RCC_APB1ENR = (volatile uint32_t*)0x40023840;
    *RCC_APB1ENR |= (1 << 17);

    // Configure USART2
    volatile uint32_t* cr1 = (volatile uint32_t*)USART2_CR1;
    volatile uint32_t* brr = (volatile uint32_t*)USART2_BRR;

    *cr1 = 0;                           // Disable USART
    *brr = 0x0683;                      // 115200 baud @ 16MHz (approximation)
    *cr1 = USART_CR1_UE | USART_CR1_TE; // Enable USART + TX

    uart_initialized = 1;
}

static void uart_putc(char c) {
    volatile uint32_t* sr = (volatile uint32_t*)USART2_SR;
    volatile uint32_t* dr = (volatile uint32_t*)USART2_DR;

    // Wait for TXE (transmit buffer empty)
    while (!(*sr & USART_SR_TXE)) {
    }
    *dr = (uint32_t)c;
}

int _write(int fd, const char* buf, int len) {
    (void)fd;
    uart_init_once();

    for (int i = 0; i < len; ++i) {
        if (buf[i] == '\n') {
            uart_putc('\r'); // CR before LF for terminal compatibility
        }
        uart_putc(buf[i]);
    }
    return len;
}

int _read(int fd, char* buf, int len) {
    (void)fd;
    (void)buf;
    (void)len;
    return 0; // EOF
}

int _close(int fd) {
    (void)fd;
    return -1;
}
int _fstat(int fd, struct stat* st) {
    (void)fd;
    st->st_mode = S_IFCHR;
    return 0;
}
int _isatty(int fd) {
    return (fd < 3) ? 1 : 0;
}
int _lseek(int fd, int offset, int whence) {
    (void)fd;
    (void)offset;
    (void)whence;
    return 0;
}
int _getpid() {
    return 1;
}
int _kill(int pid, int sig) {
    (void)pid;
    (void)sig;
    return -1;
}

void _exit(int status) {
    (void)status;
    while (1)
        ;
}

} // extern "C"
