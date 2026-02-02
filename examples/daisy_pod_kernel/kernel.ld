/* Daisy Seed linker script - Internal Flash execution */
/* STM32H750IB + IS25LP064A (QSPI) + IS42S16160J (SDRAM) */

ENTRY(Reset_Handler)

INCLUDE memory.ld

_estack = 0x20020000;  /* End of DTCMRAM (initial stack pointer) */

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        _stext = .;
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .ARM.extab :
    {
        . = ALIGN(4);
        *(.ARM.extab)
        *(.gnu.linkonce.armextab.*)
        . = ALIGN(4);
    } > FLASH

    .exidx :
    {
        . = ALIGN(4);
        PROVIDE(__exidx_start = .);
        *(.ARM.exidx*)
        . = ALIGN(4);
        PROVIDE(__exidx_end = .);
    } > FLASH

    .ARM.attributes 0 : { *(.ARM.attributes) }

    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(.fini_array*))
        KEEP(*(SORT(.fini_array.*)))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    /* Initialized data (copied from FLASH to SRAM_D1 at startup) */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        PROVIDE(__data_start__ = _sdata);
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
        PROVIDE(__data_end__ = _edata);
    } > SRAM_D1 AT >FLASH

    _sidata = LOADADDR(.data);

    /* Uninitialized data */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        PROVIDE(__bss_start__ = _sbss);
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        PROVIDE(__bss_end__ = _ebss);
    } > SRAM_D1

    PROVIDE(end = .);

    /* DTCM RAM section (fast, no DMA, for stack/performance-critical data) */
    .dtcmram_bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sdtcm_bss = .;
        *(.dtcmram_bss)
        *(.dtcmram_bss*)
        . = ALIGN(4);
        _edtcm_bss = .;
    } > DTCMRAM

    /* D2 SRAM section (DMA-accessible, for audio/USB buffers) */
    .sram_d2_bss (NOLOAD) :
    {
        . = ALIGN(4);
        *(.sram1_bss)
        *(.sram1_bss*)
        *(.sram_d2_bss)
        *(.sram_d2_bss*)
        . = ALIGN(4);
    } > SRAM_D2

    /* SDRAM section (external, for large buffers) */
    .sdram_bss (NOLOAD) :
    {
        . = ALIGN(4);
        *(.sdram_bss)
        *(.sdram_bss*)
        . = ALIGN(4);
    } > SDRAM

    /* Backup SRAM (battery-backed) */
    .backup_sram (NOLOAD) :
    {
        . = ALIGN(4);
        *(.backup_sram)
        *(.backup_sram*)
        . = ALIGN(4);
    } > BACKUP_SRAM

    /* QSPI Flash mapped data/code */
    .qspiflash_text :
    {
        . = ALIGN(4);
        *(.qspiflash_text)
        *(.qspiflash_text*)
        . = ALIGN(4);
    } > QSPIFLASH

    /* Heap (in main SRAM) */
    .heap (NOLOAD) :
    {
        . = ALIGN(4);
        PROVIDE(__heap_start = .);
        KEEP(*(.heap))
        . = ALIGN(4);
        PROVIDE(__heap_end = .);
    } > SRAM_D1

    /* Shared memory (kernel ↔ app communication) */
    /* Located at end of SRAM_D1: 0x24070000, 64KB */
    .shared 0x24070000 (NOLOAD) :
    {
        . = ALIGN(4);
        _shared_start = .;
        *(.shared)
        *(.shared*)
        . = ALIGN(4);
        _shared_end = .;
    } > SRAM_D1

    /* Application image entry point (QSPI XIP) */
    _app_image_start = ORIGIN(QSPIFLASH);

    /* Application RAM region: 0x24040000, 192KB */
    _app_ram_start = 0x24040000;
    _app_ram_size  = 192K;
}
