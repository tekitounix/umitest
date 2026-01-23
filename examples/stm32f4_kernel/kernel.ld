/* SPDX-License-Identifier: MIT */
/* STM32F4-Discovery Kernel Linker Script */
/* Kernel occupies lower flash, application loaded to RAM */

MEMORY {
    /* Kernel in lower flash (384KB) */
    FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 384K
    
    /* Application image storage in upper flash (128KB) */
    APP_FLASH (rx)  : ORIGIN = 0x08060000, LENGTH = 128K
    
    /* Kernel RAM (64KB CCM - fast, no DMA) */
    CCM (rwx)       : ORIGIN = 0x10000000, LENGTH = 64K
    
    /* Main SRAM (112KB) - split between kernel and app */
    /* Kernel: 48KB */
    SRAM (rwx)      : ORIGIN = 0x20000000, LENGTH = 48K
    
    /* Application RAM (48KB) */
    APP_RAM (rwx)   : ORIGIN = 0x2000C000, LENGTH = 48K
    
    /* Shared memory (16KB) - accessible by both kernel and app */
    SHARED (rwx)    : ORIGIN = 0x20018000, LENGTH = 16K
}

/* Entry point */
ENTRY(Reset_Handler)

/* Stack sizes */
_kernel_stack_size = 4K;
_isr_stack_size = 2K;

SECTIONS {
    /* ================================================================== */
    /* Vector Table (must be at 0x08000000) */
    /* ================================================================== */
    
    .isr_vector : {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH
    
    /* ================================================================== */
    /* Kernel Code */
    /* ================================================================== */
    
    .text : {
        . = ALIGN(4);
        _stext = .;
        
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        
        KEEP(*(.init))
        KEEP(*(.fini))
        
        . = ALIGN(4);
        _etext = .;
    } > FLASH
    
    /* ================================================================== */
    /* Read-Only Data */
    /* ================================================================== */
    
    .rodata : {
        . = ALIGN(4);
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH
    
    /* ================================================================== */
    /* ARM Exception Tables */
    /* ================================================================== */
    
    .ARM.extab : {
        *(.ARM.extab*)
    } > FLASH
    
    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH
    
    /* ================================================================== */
    /* Constructors/Destructors */
    /* ================================================================== */
    
    .init_array : {
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(.init_array*))
        KEEP(*(SORT(.init_array.*)))
        __init_array_end = .;
    } > FLASH
    
    .fini_array : {
        . = ALIGN(4);
        __fini_array_start = .;
        KEEP(*(.fini_array*))
        KEEP(*(SORT(.fini_array.*)))
        __fini_array_end = .;
    } > FLASH
    
    _sidata = LOADADDR(.data);
    
    /* ================================================================== */
    /* Initialized Data (in SRAM) */
    /* ================================================================== */
    
    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH
    
    /* ================================================================== */
    /* Uninitialized Data (in SRAM) */
    /* ================================================================== */
    
    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM
    
    /* ================================================================== */
    /* DMA Buffers (in SRAM, not CCM - DMA cannot access CCM) */
    /* ================================================================== */
    
    .dma_buffer (NOLOAD) : {
        . = ALIGN(4);
        *(.dma_buffer*)
        . = ALIGN(4);
    } > SRAM

    /* ================================================================== */
    /* Debug Buffers (in CCM to reduce SRAM pressure) */
    /* ================================================================== */

    .debug_ccm (NOLOAD) : {
        . = ALIGN(4);
        *(.debug_ccm*)
        . = ALIGN(4);
    } > CCM

    /* ================================================================== */
    /* Audio Processing Buffers (in CCM) */
    /* ================================================================== */

    .audio_ccm (NOLOAD) : {
        . = ALIGN(4);
        *(.audio_ccm*)
        . = ALIGN(4);
    } > CCM
    
    /* ================================================================== */
    /* Kernel Stack (in CCM for speed) */
    /* ================================================================== */
    
    .stack (NOLOAD) : {
        . = ALIGN(8);
        _sstack = .;
        . = . + _kernel_stack_size;
        _ekernel_stack = .;
    } > CCM
    
    /* ================================================================== */
    /* ISR Stack (in CCM) - Initial SP for vector table */
    /* ================================================================== */
    
    .isr_stack (NOLOAD) : {
        . = ALIGN(8);
        _sisr_stack = .;
        . = . + _isr_stack_size;
        _estack = .;  /* This is the initial SP for the vector table */
    } > CCM
    
    /* ================================================================== */
    /* Shared Memory Region */
    /* ================================================================== */
    
    .shared (NOLOAD) : {
        . = ALIGN(4);
        _sshared = .;
        *(.shared*)
        . = ALIGN(4);
        _eshared = .;
    } > SHARED
    
    /* ================================================================== */
    /* Application Image Location (in upper flash) */
    /* ================================================================== */
    
    _app_image_start = ORIGIN(APP_FLASH);
    _app_image_end = ORIGIN(APP_FLASH) + LENGTH(APP_FLASH);
    
    /* Application RAM region (for loader) */
    _app_ram_start = ORIGIN(APP_RAM);
    _app_ram_end = ORIGIN(APP_RAM) + LENGTH(APP_RAM);
    _app_ram_size = LENGTH(APP_RAM);
    
    /* Shared memory region (for loader) */
    _shared_start = ORIGIN(SHARED);
    _shared_end = ORIGIN(SHARED) + LENGTH(SHARED);
    _shared_size = LENGTH(SHARED);
    
    /* ================================================================== */
    /* Discard */
    /* ================================================================== */
    
    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
    }
}

/* _estack is defined directly in .isr_stack section */
