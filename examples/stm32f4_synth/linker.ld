/* STM32F407VG Linker Script for UMI-OS Synth */
/* STM32F4-Discovery: 1MB Flash, 192KB RAM (128KB SRAM + 64KB CCM) */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    SRAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
    CCM (rw)    : ORIGIN = 0x10000000, LENGTH = 64K
}

/* Stack and heap sizes */
_stack_size = 4K;
_heap_size = 16K;

/* Audio buffer configuration */
_audio_buffer_size = 512;  /* samples per channel */

SECTIONS
{
    /* Vector table at start of flash */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    /* Code */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* Init arrays for C++ */
    .preinit_array :
    {
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP(*(.preinit_array*))
        __preinit_array_end = .;
    } > FLASH

    .init_array :
    {
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        __init_array_end = .;
    } > FLASH

    .fini_array :
    {
        . = ALIGN(4);
        __fini_array_start = .;
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        __fini_array_end = .;
    } > FLASH

    /* ARM exception handling */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    _sidata = LOADADDR(.data);

    /* Initialized data */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH

    /* Uninitialized data */
    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > SRAM

    /* Kernel data (32KB) */
    .kernel_ram (NOLOAD) :
    {
        . = ALIGN(4);
        _kernel_ram_start = .;
        . = . + 32K;
        _kernel_ram_end = .;
    } > SRAM

    /* Shared memory region (16KB) */
    .shared (NOLOAD) :
    {
        . = ALIGN(4);
        _shared_start = .;

        /* Audio buffer (8KB) */
        _shared_audio = .;
        . = . + 8K;

        /* MIDI buffer (2KB) */
        _shared_midi = .;
        . = . + 2K;

        /* HW state (1KB) */
        _shared_hwstate = .;
        . = . + 1K;

        /* Display (2KB) */
        _shared_display = .;
        . = . + 2K;

        /* App regions (3KB) */
        _shared_app = .;
        . = . + 3K;

        _shared_end = .;
    } > SRAM

    /* User stack/heap */
    .user_ram (NOLOAD) :
    {
        . = ALIGN(8);
        _user_ram_start = .;
        _heap_start = .;
        . = . + _heap_size;
        _heap_end = .;
        . = ALIGN(8);
        _stack_end = .;
        . = . + _stack_size;
        _stack_start = .;
        _estack = .;
    } > SRAM

    /* CCM: Audio processing buffers (fast, no DMA access) */
    .ccm (NOLOAD) :
    {
        . = ALIGN(4);
        _ccm_start = .;
        *(.ccm)
        *(.ccm*)

        /* DSP work buffers */
        _dsp_buffer_start = .;
        . = . + 16K;
        _dsp_buffer_end = .;

        _ccm_end = .;
    } > CCM

    /* DMA buffers must be in SRAM (not CCM) */
    .dma_buffers (NOLOAD) :
    {
        . = ALIGN(4);
        _dma_buffer_start = .;
        *(.dma_buffer)
        *(.dma_buffer*)
        _dma_buffer_end = .;
    } > SRAM
}

/* Provide symbols */
PROVIDE(_stack_top = _estack);
PROVIDE(_Min_Stack_Size = _stack_size);
PROVIDE(_Min_Heap_Size = _heap_size);
