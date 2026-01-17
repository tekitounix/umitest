/* STM32F4 Linker Script for UMI-OS */
/* Typical STM32F407 or STM32F446 configuration */

ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
    SRAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
    CCM (rwx)   : ORIGIN = 0x10000000, LENGTH = 64K   /* Core Coupled Memory - fast */
}

/* Stack and heap sizes */
_stack_size = 8K;
_heap_size = 32K;

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
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    /* Pre-init and init arrays for C++ static constructors */
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

    /* ARM exception handling (required even with -fno-exceptions for unwinding) */
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

    /* Initialized data (copied from flash to RAM at startup) */
    _sidata = LOADADDR(.data);
    
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH

    /* Uninitialized data (zeroed at startup) */
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
        _end = .;          /* End of BSS, used by sbrk */
    } > SRAM

    /* Heap */
    .heap :
    {
        . = ALIGN(8);
        _heap_start = .;
        . = . + _heap_size;
        _heap_end = .;
    } > SRAM

    /* Stack (grows downward) */
    .stack :
    {
        . = ALIGN(8);
        _stack_end = .;
        . = . + _stack_size;
        _stack_start = .;
        _estack = .;
    } > SRAM

    /* Audio buffers in CCM (fast, no DMA access) */
    .ccm (NOLOAD) :
    {
        . = ALIGN(4);
        *(.ccm)
        *(.ccm*)
    } > CCM

    /* Shared memory region (for kernel/app communication) */
    .shared (NOLOAD) :
    {
        . = ALIGN(4);
        _shared_start = .;
        *(.shared)
        *(.shared*)
        _shared_end = .;
    } > SRAM

    /* Shared audio buffer region */
    .shared_audio (NOLOAD) :
    {
        . = ALIGN(4);
        _shared_audio_start = .;
        *(.shared_audio)
        *(.shared_audio*)
        _shared_audio_end = .;
    } > SRAM

    /* Shared MIDI buffer region */
    .shared_midi (NOLOAD) :
    {
        . = ALIGN(4);
        _shared_midi_start = .;
        *(.shared_midi)
        *(.shared_midi*)
        _shared_midi_end = .;
    } > SRAM

    /* Shared framebuffer region */
    .shared_fb (NOLOAD) :
    {
        . = ALIGN(4);
        _shared_fb_start = .;
        *(.shared_fb)
        *(.shared_fb*)
        _shared_fb_end = .;
    } > SRAM

    /* Shared hardware state region */
    .shared_hwstate (NOLOAD) :
    {
        . = ALIGN(4);
        _shared_hwstate_start = .;
        *(.shared_hwstate)
        *(.shared_hwstate*)
        _shared_hwstate_end = .;
    } > SRAM
}

/* Provide symbols for startup code */
PROVIDE(_stack_top = _estack);
PROVIDE(_data_start = _sdata);
PROVIDE(_data_end = _edata);
PROVIDE(_data_loadaddr = _sidata);
PROVIDE(_bss_start = _sbss);
PROVIDE(_bss_end = _ebss);
