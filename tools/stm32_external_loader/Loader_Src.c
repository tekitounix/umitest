// SPDX-License-Identifier: BSD-3-Clause
// IS25LP064A External Loader for STM32CubeProgrammer
// Target: Daisy Seed (STM32H750VBT6), QSPI flash at 0x90000000
//
// Bare-metal implementation without HAL dependency.
// Clock: HSE 16MHz → PLL1 (M=4,N=240,P=2) → SYSCLK 480MHz, AHB 240MHz
// QSPI: HCLK/2 = 120MHz → prescaler 1 → 60MHz actual

#include <stdint.h>
#include <string.h>

#include "Dev_Inf.h"

// ---- Register addresses ----
#define RCC_BASE        0x58024400
#define RCC_CR          (*(volatile uint32_t*)(RCC_BASE + 0x00))
#define RCC_CFGR        (*(volatile uint32_t*)(RCC_BASE + 0x10))
#define RCC_D1CFGR      (*(volatile uint32_t*)(RCC_BASE + 0x18))
#define RCC_D2CFGR      (*(volatile uint32_t*)(RCC_BASE + 0x1C))
#define RCC_D3CFGR      (*(volatile uint32_t*)(RCC_BASE + 0x20))
#define RCC_PLLCKSELR   (*(volatile uint32_t*)(RCC_BASE + 0x28))
#define RCC_PLLCFGR     (*(volatile uint32_t*)(RCC_BASE + 0x2C))
#define RCC_PLL1DIVR    (*(volatile uint32_t*)(RCC_BASE + 0x30))
#define RCC_AHB3ENR     (*(volatile uint32_t*)(RCC_BASE + 0xD4))
#define RCC_AHB4ENR     (*(volatile uint32_t*)(RCC_BASE + 0xE0))
#define RCC_APB4ENR     (*(volatile uint32_t*)(RCC_BASE + 0xF4))

#define PWR_BASE        0x58024800
#define PWR_CR3         (*(volatile uint32_t*)(PWR_BASE + 0x0C))
#define PWR_CSR1        (*(volatile uint32_t*)(PWR_BASE + 0x04))
#define PWR_D3CR        (*(volatile uint32_t*)(PWR_BASE + 0x18))

#define FLASH_BASE_R    0x52002000
#define FLASH_ACR       (*(volatile uint32_t*)(FLASH_BASE_R + 0x00))

#define SYSCFG_PWRCR    (*(volatile uint32_t*)(0x58000404))

// GPIO
#define GPIOF_BASE      0x58021400
#define GPIOF_MODER     (*(volatile uint32_t*)(GPIOF_BASE + 0x00))
#define GPIOF_OSPEEDR   (*(volatile uint32_t*)(GPIOF_BASE + 0x08))
#define GPIOF_PUPDR     (*(volatile uint32_t*)(GPIOF_BASE + 0x0C))
#define GPIOF_AFRL      (*(volatile uint32_t*)(GPIOF_BASE + 0x20))
#define GPIOF_AFRH      (*(volatile uint32_t*)(GPIOF_BASE + 0x24))

#define GPIOG_BASE      0x58021800
#define GPIOG_MODER     (*(volatile uint32_t*)(GPIOG_BASE + 0x00))
#define GPIOG_OSPEEDR   (*(volatile uint32_t*)(GPIOG_BASE + 0x08))
#define GPIOG_PUPDR     (*(volatile uint32_t*)(GPIOG_BASE + 0x0C))
#define GPIOG_AFRL      (*(volatile uint32_t*)(GPIOG_BASE + 0x20))

// QUADSPI
#define QUADSPI_BASE    0x52005000
#define QUADSPI_CR      (*(volatile uint32_t*)(QUADSPI_BASE + 0x00))
#define QUADSPI_DCR     (*(volatile uint32_t*)(QUADSPI_BASE + 0x04))
#define QUADSPI_SR      (*(volatile uint32_t*)(QUADSPI_BASE + 0x08))
#define QUADSPI_FCR     (*(volatile uint32_t*)(QUADSPI_BASE + 0x0C))
#define QUADSPI_DLR     (*(volatile uint32_t*)(QUADSPI_BASE + 0x10))
#define QUADSPI_CCR     (*(volatile uint32_t*)(QUADSPI_BASE + 0x14))
#define QUADSPI_AR      (*(volatile uint32_t*)(QUADSPI_BASE + 0x18))
#define QUADSPI_DR      (*(volatile uint32_t*)(QUADSPI_BASE + 0x20))
#define QUADSPI_PSMKR   (*(volatile uint32_t*)(QUADSPI_BASE + 0x24))
#define QUADSPI_PSMAR   (*(volatile uint32_t*)(QUADSPI_BASE + 0x28))
#define QUADSPI_PIR     (*(volatile uint32_t*)(QUADSPI_BASE + 0x2C))

// IS25LP064A commands
#define CMD_READ_STATUS   0x05
#define CMD_WRITE_ENABLE  0x06
#define CMD_PAGE_PROGRAM  0x02
#define CMD_SECTOR_ERASE  0x20   // 4KB
#define CMD_BLOCK_ERASE   0xD8   // 64KB
#define CMD_CHIP_ERASE    0xC7
#define CMD_QUAD_READ     0xEB
#define CMD_RESET_ENABLE  0x66
#define CMD_RESET_DEVICE  0x99

// QSPI mode constants
#define MODE_NONE    0
#define MODE_SINGLE  1
#define MODE_DUAL    2
#define MODE_QUAD    3

#define FMODE_INDIRECT_WRITE  0
#define FMODE_INDIRECT_READ   1
#define FMODE_AUTO_POLLING    2
#define FMODE_MEMORY_MAPPED   3

#define ADSIZE_24BIT  2

// ---- Stubs for CubeProgrammer ----
// The loader runs in a special context where SysTick is not available
uint32_t SystemCoreClock = 480000000;

static void delay_cycles(volatile uint32_t n) {
    while (n--) { __asm volatile("nop"); }
}

// ---- Clock configuration ----
// Daisy Seed: HSE 16MHz → PLL1 → 480MHz SYSCLK
static void SystemClock_Config(void) {
    // Enable SYSCFG clock
    RCC_APB4ENR |= (1U << 1);  // SYSCFGEN

    // Configure power: LDO mode
    PWR_CR3 = (PWR_CR3 & ~((1U << 2) | (1U << 0))) | (1U << 1);  // LDOEN=1, SDEN=0, BYPASS=0
    while (!(PWR_CSR1 & (1U << 13))) {}  // Wait ACTVOSRDY

    // VOS1
    PWR_D3CR = (PWR_D3CR & ~(3U << 14)) | (3U << 14);  // VOS=SCALE1
    while (!(PWR_D3CR & (1U << 13))) {}  // Wait VOSRDY

    // Enable HSE
    RCC_CR |= (1U << 16);  // HSEON
    while (!(RCC_CR & (1U << 17))) {}  // HSERDY

    // PLL1: HSE/4 = 4MHz → *240 = 960MHz VCO → /2 = 480MHz
    RCC_PLLCKSELR = (RCC_PLLCKSELR & ~0x3F0003U) | (4U << 4) | (2U << 0);  // DIVM1=4, SRC=HSE

    // PLL1 config: wide VCO, 4-8MHz input, P/Q enabled
    RCC_PLLCFGR = (RCC_PLLCFGR & ~0x1FFU) |
                  (0U << 1) |   // PLL1VCOSEL=wide
                  (2U << 2) |   // PLL1RGE=4-8MHz (0b10)
                  (1U << 4) |   // DIVP1EN
                  (1U << 5);    // DIVQ1EN

    // PLL1 dividers: N=240, P=2
    RCC_PLL1DIVR = (239U << 0) | (1U << 9);  // DIVN=239, DIVP=1

    // Enable PLL1
    RCC_CR |= (1U << 24);  // PLL1ON
    while (!(RCC_CR & (1U << 25))) {}  // PLL1RDY

    // Flash latency: 4 WS
    FLASH_ACR = (FLASH_ACR & ~0x1FU) | 4U | (2U << 4);  // LATENCY=4, WRHIGHFREQ=10

    // Bus dividers: AHB/2=240MHz, APBx/2=120MHz
    RCC_D1CFGR = (0U << 8) | (8U << 4) | (4U << 0);  // D1CPRE=/1, HPRE=/2, D1PPRE=/2
    RCC_D2CFGR = (4U << 8) | (4U << 4);  // D2PPRE2=/2, D2PPRE1=/2
    RCC_D3CFGR = (4U << 4);  // D3PPRE=/2

    // Switch to PLL1
    RCC_CFGR = (RCC_CFGR & ~7U) | 3U;  // SW=PLL1
    while (((RCC_CFGR >> 3) & 7U) != 3U) {}  // Wait SWS=PLL1

    // Enable VOS0 (boost) for 480MHz
    SYSCFG_PWRCR |= 1U;
    while (!(PWR_D3CR & (1U << 13))) {}
}

// ---- GPIO configuration for QSPI ----
static void gpio_set_af(volatile uint32_t* moder, volatile uint32_t* ospeedr,
                         volatile uint32_t* pupdr, volatile uint32_t* afrl,
                         volatile uint32_t* afrh, uint8_t pin, uint8_t af, uint8_t pupd) {
    // Mode = AF (0b10)
    *moder = (*moder & ~(3U << (pin * 2))) | (2U << (pin * 2));
    // Speed = Very High (0b11)
    *ospeedr = (*ospeedr & ~(3U << (pin * 2))) | (3U << (pin * 2));
    // Pull-up/down
    *pupdr = (*pupdr & ~(3U << (pin * 2))) | ((uint32_t)pupd << (pin * 2));
    // AF
    if (pin < 8) {
        *afrl = (*afrl & ~(0xFU << (pin * 4))) | ((uint32_t)af << (pin * 4));
    } else {
        *afrh = (*afrh & ~(0xFU << ((pin - 8) * 4))) | ((uint32_t)af << ((pin - 8) * 4));
    }
}

static void init_qspi_gpio(void) {
    // Enable GPIOF and GPIOG clocks
    RCC_AHB4ENR |= (1U << 5) | (1U << 6);
    delay_cycles(16);

    // PF6=IO3(AF9), PF7=IO2(AF9), PF8=IO0(AF10), PF9=IO1(AF10), PF10=CLK(AF9)
    gpio_set_af(&GPIOF_MODER, &GPIOF_OSPEEDR, &GPIOF_PUPDR, &GPIOF_AFRL, &GPIOF_AFRH, 6, 9, 0);
    gpio_set_af(&GPIOF_MODER, &GPIOF_OSPEEDR, &GPIOF_PUPDR, &GPIOF_AFRL, &GPIOF_AFRH, 7, 9, 0);
    gpio_set_af(&GPIOF_MODER, &GPIOF_OSPEEDR, &GPIOF_PUPDR, &GPIOF_AFRL, &GPIOF_AFRH, 8, 10, 0);
    gpio_set_af(&GPIOF_MODER, &GPIOF_OSPEEDR, &GPIOF_PUPDR, &GPIOF_AFRL, &GPIOF_AFRH, 9, 10, 0);
    gpio_set_af(&GPIOF_MODER, &GPIOF_OSPEEDR, &GPIOF_PUPDR, &GPIOF_AFRL, &GPIOF_AFRH, 10, 9, 0);

    // PG6=NCS(AF10), pull-up
    gpio_set_af(&GPIOG_MODER, &GPIOG_OSPEEDR, &GPIOG_PUPDR, &GPIOG_AFRL, NULL, 6, 10, 1);
}

// ---- QSPI helpers ----
static void qspi_wait_busy(void) {
    while (QUADSPI_SR & (1U << 5)) {}
}

static void qspi_wait_tc(void) {
    while (!(QUADSPI_SR & (1U << 1))) {}
    QUADSPI_FCR = (1U << 1);  // Clear TCF
}

static void qspi_abort(void) {
    QUADSPI_CR |= (1U << 1);  // ABORT
    while (QUADSPI_CR & (1U << 1)) {}
}

// Send single-line command (no address, no data)
static void qspi_command(uint8_t cmd) {
    qspi_abort();
    qspi_wait_busy();
    QUADSPI_CCR = (uint32_t)cmd |
                  (MODE_SINGLE << 8) |
                  (MODE_NONE << 10) |
                  (MODE_NONE << 24) |
                  (FMODE_INDIRECT_WRITE << 26);
    qspi_wait_tc();
}

// Auto-poll status register for WIP=0
static void qspi_wait_wip(void) {
    qspi_abort();
    qspi_wait_busy();

    QUADSPI_CR |= (1U << 22);  // APMS
    QUADSPI_DLR = 0;
    QUADSPI_PSMKR = 0x01;      // Mask: WIP bit
    QUADSPI_PSMAR = 0x00;      // Match: WIP=0
    QUADSPI_PIR = 0x10;

    QUADSPI_CCR = (uint32_t)CMD_READ_STATUS |
                  (MODE_SINGLE << 8) |
                  (MODE_NONE << 10) |
                  (MODE_SINGLE << 24) |
                  (FMODE_AUTO_POLLING << 26);

    while (!(QUADSPI_SR & (1U << 3))) {}  // Wait SMF
    QUADSPI_FCR = (1U << 3);              // Clear SMF
    qspi_wait_busy();
}

static void qspi_write_enable(void) {
    qspi_command(CMD_WRITE_ENABLE);
}

// Enter memory-mapped mode (quad read 0xEB)
static void qspi_memory_mapped(void) {
    qspi_abort();
    qspi_wait_busy();
    QUADSPI_CCR = (uint32_t)CMD_QUAD_READ |
                  (MODE_SINGLE << 8) |
                  (MODE_QUAD << 10) |
                  (ADSIZE_24BIT << 12) |
                  (MODE_QUAD << 24) |
                  (6U << 18) |                    // 6 dummy cycles
                  (FMODE_MEMORY_MAPPED << 26) |
                  (1U << 28);                     // SIOO
}

// ---- QSPI init ----
static void init_qspi(void) {
    // Enable QSPI clock
    RCC_AHB3ENR |= (1U << 14);
    delay_cycles(16);

    init_qspi_gpio();

    // Abort and disable
    qspi_abort();
    QUADSPI_CR &= ~1U;  // Disable

    // Configure: prescaler=1 (120MHz/2=60MHz QSPI clock)
    QUADSPI_CR = (1U << 24);  // PRESCALER=1, SSHIFT=0, FTHRES=0

    // Device config: 8MB (FSIZE=22), CSHT=2 cycles
    QUADSPI_DCR = (22U << 16) | (1U << 8);

    // Enable
    QUADSPI_CR |= 1U;

    // Reset flash
    qspi_command(CMD_RESET_ENABLE);
    qspi_command(CMD_RESET_DEVICE);
    delay_cycles(10000);

    qspi_wait_wip();
}

// ---- Page program (max 256 bytes) ----
static int qspi_page_program(uint32_t addr, const uint8_t* data, uint32_t size) {
    qspi_write_enable();
    qspi_abort();
    qspi_wait_busy();

    QUADSPI_DLR = size - 1;

    // Page program: single-line instruction + 24-bit address + single-line data
    QUADSPI_CCR = (uint32_t)CMD_PAGE_PROGRAM |
                  (MODE_SINGLE << 8) |
                  (MODE_SINGLE << 10) |
                  (ADSIZE_24BIT << 12) |
                  (MODE_SINGLE << 24) |
                  (FMODE_INDIRECT_WRITE << 26);

    QUADSPI_AR = addr;

    // Write data byte by byte through DR
    for (uint32_t i = 0; i < size; i++) {
        while (!(QUADSPI_SR & (1U << 2))) {}  // Wait FTF
        *(volatile uint8_t*)&QUADSPI_DR = data[i];
    }

    qspi_wait_tc();
    qspi_wait_wip();

    return 1;
}

// ==== External Loader API ====

__attribute__((used)) int Init(void) {
    SystemClock_Config();
    init_qspi();
    qspi_memory_mapped();
    return 1;
}

__attribute__((used)) int Write(uint32_t Address, uint32_t Size, uint8_t* buffer) {
    Address &= 0x0FFFFFFF;

    // Exit memory-mapped mode
    qspi_abort();

    while (Size > 0) {
        uint32_t chunk = 256 - (Address & 0xFF);  // Align to page boundary
        if (chunk > Size) chunk = Size;
        if (!qspi_page_program(Address, buffer, chunk)) return 0;
        Address += chunk;
        buffer += chunk;
        Size -= chunk;
    }

    // Re-enter memory-mapped mode
    qspi_memory_mapped();
    return 1;
}

__attribute__((used)) int SectorErase(uint32_t EraseStartAddress, uint32_t EraseEndAddress) {
    EraseStartAddress &= 0x0FFFFFFF;
    EraseEndAddress &= 0x0FFFFFFF;
    EraseStartAddress -= EraseStartAddress % 0x1000;  // Align to 4KB

    // Exit memory-mapped mode
    qspi_abort();

    while (EraseStartAddress <= EraseEndAddress) {
        qspi_write_enable();
        qspi_abort();
        qspi_wait_busy();

        // Sector erase: single-line cmd + 24-bit address, no data
        QUADSPI_CCR = (uint32_t)CMD_SECTOR_ERASE |
                      (MODE_SINGLE << 8) |
                      (MODE_SINGLE << 10) |
                      (ADSIZE_24BIT << 12) |
                      (MODE_NONE << 24) |
                      (FMODE_INDIRECT_WRITE << 26);

        QUADSPI_AR = EraseStartAddress;
        qspi_wait_tc();
        qspi_wait_wip();

        EraseStartAddress += 0x1000;  // Next 4KB sector
    }

    qspi_memory_mapped();
    return 1;
}

__attribute__((used)) int MassErase(uint32_t Parallelism) {
    (void)Parallelism;

    qspi_abort();
    qspi_write_enable();
    qspi_command(CMD_CHIP_ERASE);
    qspi_wait_wip();
    qspi_memory_mapped();
    return 1;
}

uint32_t CheckSum(uint32_t StartAddress, uint32_t Size, uint32_t InitVal) {
    uint8_t missalignementAddress = StartAddress % 4;
    uint8_t missalignementSize = Size;
    int cnt;
    uint32_t Val;

    StartAddress -= StartAddress % 4;
    Size += (Size % 4 == 0) ? 0 : 4 - (Size % 4);

    for (cnt = 0; cnt < (int)Size; cnt += 4) {
        Val = *(uint32_t*)StartAddress;
        if (missalignementAddress) {
            switch (missalignementAddress) {
                case 1: InitVal += (uint8_t)(Val >> 8 & 0xff);
                        InitVal += (uint8_t)(Val >> 16 & 0xff);
                        InitVal += (uint8_t)(Val >> 24 & 0xff);
                        missalignementAddress -= 1; break;
                case 2: InitVal += (uint8_t)(Val >> 16 & 0xff);
                        InitVal += (uint8_t)(Val >> 24 & 0xff);
                        missalignementAddress -= 2; break;
                case 3: InitVal += (uint8_t)(Val >> 24 & 0xff);
                        missalignementAddress -= 3; break;
            }
        } else if ((Size - missalignementSize) % 4 && ((uint32_t)(Size - cnt) <= 4)) {
            switch (Size - missalignementSize) {
                case 1: InitVal += (uint8_t)Val;
                        InitVal += (uint8_t)(Val >> 8 & 0xff);
                        InitVal += (uint8_t)(Val >> 16 & 0xff);
                        missalignementSize -= 1; break;
                case 2: InitVal += (uint8_t)Val;
                        InitVal += (uint8_t)(Val >> 8 & 0xff);
                        missalignementSize -= 2; break;
                case 3: InitVal += (uint8_t)Val;
                        missalignementSize -= 3; break;
            }
        } else {
            InitVal += (uint8_t)Val;
            InitVal += (uint8_t)(Val >> 8 & 0xff);
            InitVal += (uint8_t)(Val >> 16 & 0xff);
            InitVal += (uint8_t)(Val >> 24 & 0xff);
        }
        StartAddress += 4;
    }
    return InitVal;
}

__attribute__((used)) uint64_t Verify(uint32_t MemoryAddr, uint32_t RAMBufferAddr, uint32_t Size, uint32_t missalignement) {
    uint32_t VerifiedData = 0, InitVal = 0;
    uint64_t checksum;
    Size *= 4;
    checksum = CheckSum((uint32_t)MemoryAddr + (missalignement & 0xF),
                        Size - ((missalignement >> 16) & 0xF), InitVal);
    while (Size > VerifiedData) {
        if (*(uint8_t*)MemoryAddr++ != *((uint8_t*)RAMBufferAddr + VerifiedData))
            return ((checksum << 32) + (MemoryAddr + VerifiedData));
        VerifiedData++;
    }
    return (checksum << 32);
}
