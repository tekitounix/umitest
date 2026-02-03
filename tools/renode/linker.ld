/* STM32F407 Linker Script for UMI-OS */
/* Memory Map:
 *   Flash:  0x08000000 - 0x08100000 (1MB)
 *   SRAM:   0x20000000 - 0x20020000 (128KB)
 *   CCM:    0x10000000 - 0x10010000 (64KB)
 *
 * SRAM Layout:
 *   0x20000000 - 0x20008000  Kernel Data (32KB)
 *   0x20008000 - 0x2000A000  Shared Audio Buffer (8KB)
 *   0x2000A000 - 0x2000A800  Shared MIDI Buffer (2KB)
 *   0x2000A800 - 0x2000AC00  Shared HwState (1KB)
 *   0x2000AC00 - 0x20020000  User Stack & Heap (~85KB)
 */

ENTRY(Reset_Handler)

/* Stack sizes */
_Min_Heap_Size = 0x2000;   /* 8KB heap */
_Min_Stack_Size = 0x1000;  /* 4KB main stack (MSP) */

MEMORY
{
    FLASH (rx)    : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM (rwx)     : ORIGIN = 0x20000000, LENGTH = 128K
    CCM (rwx)     : ORIGIN = 0x10000000, LENGTH = 64K
}

/* Shared memory regions - exported for kernel */
_shared_audio_start = 0x20008000;
_shared_audio_size  = 0x2000;      /* 8KB */
_shared_midi_start  = 0x2000A000;
_shared_midi_size   = 0x800;       /* 2KB */
_shared_hwstate_start = 0x2000A800;
_shared_hwstate_size  = 0x400;     /* 1KB */

SECTIONS
{
    /* Vector table at start of Flash */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    /* Code */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)

        KEEP(*(.init))
        KEEP(*(.fini))

        . = ALIGN(4);
        _etext = .;
    } >FLASH

    /* Read-only data */
    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } >FLASH

    /* ARM exception handling */
    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } >FLASH

    .ARM :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } >FLASH

    /* Constructor/Destructor tables */
    .preinit_array :
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } >FLASH

    .init_array :
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } >FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } >FLASH

    /* Driver registration section */
    .drivers :
    {
        PROVIDE_HIDDEN(__drivers_start = .);
        KEEP(*(.drivers))
        PROVIDE_HIDDEN(__drivers_end = .);
    } >FLASH

    /* Data initialization */
    _sidata = LOADADDR(.data);

    /* Initialized data - kernel only (0x20000000 - 0x20008000) */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } >RAM AT> FLASH

    /* BSS - kernel only */
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
    } >RAM

    /* Kernel data ends here */
    _kernel_data_end = .;

    /* Shared memory regions (fixed addresses) */
    .shared_audio (NOLOAD) :
    {
        . = ORIGIN(RAM) + 0x8000;
        _shared_audio = .;
        . += _shared_audio_size;
    } >RAM

    .shared_midi (NOLOAD) :
    {
        . = ORIGIN(RAM) + 0xA000;
        _shared_midi = .;
        . += _shared_midi_size;
    } >RAM

    .shared_hwstate (NOLOAD) :
    {
        . = ORIGIN(RAM) + 0xA800;
        _shared_hwstate = .;
        . += _shared_hwstate_size;
    } >RAM

    /* User heap and stack (0x2000AC00 - 0x20020000) */
    ._user_heap_stack (NOLOAD) :
    {
        . = ORIGIN(RAM) + 0xAC00;
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + _Min_Heap_Size;
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } >RAM

    /* Main stack pointer at end of RAM */
    _estack = ORIGIN(RAM) + LENGTH(RAM);

    /* CCM for kernel-only fast data */
    .ccm (NOLOAD) :
    {
        . = ALIGN(4);
        *(.ccm)
        *(.ccm*)
        . = ALIGN(4);
    } >CCM

    /* Discard unwanted sections */
    /DISCARD/ :
    {
        libc.a(*)
        libm.a(*)
        libgcc.a(*)
    }

    /* Memory region markers for MPU configuration */
    _flash_start = ORIGIN(FLASH);
    _flash_size = LENGTH(FLASH);
    _ram_start = ORIGIN(RAM);
    _ram_size = LENGTH(RAM);
    _kernel_ram_start = ORIGIN(RAM);
    _kernel_ram_size = 0x8000;
    _user_ram_start = ORIGIN(RAM) + 0xAC00;
    _user_ram_size = LENGTH(RAM) - 0xAC00;
}
