// Comprehensive Instruction Benchmark for STM32F4
// This is an example benchmark using the bench framework
#include <cstdint>

// Target-specific DWT (could be moved to platform layer)
struct DWT {
    static void enable() {
        *reinterpret_cast<volatile uint32_t*>(0xE000EDFC) |= 1u << 24;
        *reinterpret_cast<volatile uint32_t*>(0xE0001004) = 0;
        *reinterpret_cast<volatile uint32_t*>(0xE0001000) |= 1;
    }
    static uint32_t read() { return *reinterpret_cast<volatile uint32_t*>(0xE0001004); }
};

// Target-specific UART
struct UART {
    static void init() {
        *reinterpret_cast<volatile uint32_t*>(0x40023840) |= (1 << 17);
        *reinterpret_cast<volatile uint32_t*>(0x4000440C) = (1 << 13) | (1 << 3);
    }
    static void putc(char c) {
        while (!(*reinterpret_cast<volatile uint32_t*>(0x40004400) & 0x80));
        *reinterpret_cast<volatile uint32_t*>(0x40004404) = c;
    }
    static void puts(const char* s) { while (*s) putc(*s++); }
    static void print(uint32_t n) {
        if (n == 0) { putc('0'); return; }
        char b[12]; int i = 0;
        while (n > 0) { b[i++] = '0' + (n % 10); n /= 10; }
        while (i--) putc(b[i]);
    }
};

#define BENCH(name) __attribute__((noinline)) void bench_##name()

BENCH(add) { volatile int x=1; for(int i=0;i<100;i++) x+=1; (void)x; }
BENCH(mul) { volatile int x=2; for(int i=0;i<100;i++) x*=2; (void)x; }
BENCH(div) { volatile int x=100000; volatile int y=2; for(int i=0;i<100;i++) x=x/y; (void)x; }
BENCH(and_op) { volatile int x=0xFF; for(int i=0;i<100;i++) x&=0xAA; (void)x; }
BENCH(or_op) { volatile int x=0; for(int i=0;i<100;i++) x|=0x55; (void)x; }
BENCH(xor_op) { volatile int x=0xFF; for(int i=0;i<100;i++) x^=0x55; (void)x; }
BENCH(lsl) { volatile int x=1; for(int i=0;i<100;i++) x<<=1; (void)x; }
BENCH(lsr) { volatile int x=0x8000; for(int i=0;i<100;i++) x>>=1; (void)x; }
BENCH(ldr) {
    static volatile int arr[4]={1,2,3,4};
    volatile int sum=0;
    for(int i=0;i<100;i++) sum+=arr[i&3];
    (void)sum;
}
BENCH(str) {
    static volatile int arr[4];
    for(int i=0;i<100;i++) arr[i&3]=i;
    (void)arr;
}
BENCH(if_pred) { volatile int x=0; for(int i=0;i<100;i++) if(i>=0) x=x+1; (void)x; }
BENCH(if_mispred) { volatile int x=0; for(int i=0;i<100;i++) if(i&1) x=x+1; else x=x-1; (void)x; }
BENCH(indep) {
    volatile int a=1,b=2,c=3,d=4;
    for(int i=0;i<100;i++) { a+=1; b+=2; c+=3; d+=4; }
    (void)a;
}
BENCH(dep) { volatile int x=1; for(int i=0;i<100;i++) x=x*2+1; (void)x; }
BENCH(fadd) { volatile float x=1.0f; for(int i=0;i<100;i++) x+=1.0f; (void)x; }
BENCH(fmul) { volatile float x=1.0f; for(int i=0;i<100;i++) x*=1.1f; (void)x; }
BENCH(fdiv) { volatile float x=100.0f; volatile float y=2.0f; for(int i=0;i<100;i++) x=x/y; (void)x; }
BENCH(empty) { volatile int u=0; for(int i=0;i<100;i++) u=i; (void)u; }

template<int N>
uint32_t measure(void (*f)()) {
    uint32_t min = 0xFFFFFFFF;
    for (int i = 0; i < 7; i++) {
        uint32_t s = DWT::read();
        f();
        uint32_t e = DWT::read();
        if (e - s < min) min = e - s;
    }
    return min;
}

void report(const char* name, uint32_t empty, uint32_t measured, uint32_t expected) {
    UART::puts("  "); UART::puts(name); UART::puts(": ");
    UART::print(measured); UART::puts(" cy (net=");
    uint32_t net = (measured > empty) ? measured - empty : 0;
    UART::print(net/100); UART::puts("/iter exp=");
    UART::print(expected); UART::puts(")\n");
}

int main() {
    DWT::enable(); UART::init();
    UART::puts("\n\n=== Comprehensive Instruction Benchmark ===\n");

    uint32_t empty = measure<100>(bench_empty);
    UART::puts("Baseline: "); UART::print(empty); UART::puts(" cy\n\n");

    UART::puts("Arithmetic:\n");
    report("ADD", empty, measure<100>(bench_add), 1);
    report("MUL", empty, measure<100>(bench_mul), 1);
    report("DIV", empty, measure<100>(bench_div), 12);

    UART::puts("\nLogic:\n");
    report("AND", empty, measure<100>(bench_and_op), 1);
    report("OR", empty, measure<100>(bench_or_op), 1);
    report("XOR", empty, measure<100>(bench_xor_op), 1);
    report("LSL", empty, measure<100>(bench_lsl), 1);
    report("LSR", empty, measure<100>(bench_lsr), 1);

    UART::puts("\nMemory:\n");
    report("LDR", empty, measure<100>(bench_ldr), 2);
    report("STR", empty, measure<100>(bench_str), 2);

    UART::puts("\nBranch:\n");
    report("Predicted", empty, measure<100>(bench_if_pred), 2);
    report("Mispredict", empty, measure<100>(bench_if_mispred), 3);

    UART::puts("\nPipeline:\n");
    report("Independent", empty, measure<100>(bench_indep), 0);
    report("Dependent", empty, measure<100>(bench_dep), 0);

    UART::puts("\nFloat:\n");
    report("FADD", empty, measure<100>(bench_fadd), 1);
    report("FMUL", empty, measure<100>(bench_fmul), 1);
    report("FDIV", empty, measure<100>(bench_fdiv), 14);

    UART::puts("\n=== Done ===\n");
    while (1) asm volatile("wfi");
    return 0;
}
