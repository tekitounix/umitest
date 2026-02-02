/* Daisy Pod H7 Application Linker Script */
/* XIP from QSPI, data/bss/stack in APP_RAM */

MEMORY
{
    /* Application code executes from QSPI (XIP) */
    /* First 128 bytes reserved for AppHeader */
    APP_FLASH (rx)  : ORIGIN = 0x90000080, LENGTH = 8M - 128

    /* Application RAM in SRAM_D1 */
    APP_RAM   (rwx) : ORIGIN = 0x24040000, LENGTH = 192K
}

/* AppHeader is placed at 0x90000000 by make_umia tool */
_app_header = 0x90000000;

ENTRY(_start)

SECTIONS
{
    .text : ALIGN(4)
    {
        *(.text._start)
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        _etext = .;
    } > APP_FLASH

    .data : ALIGN(4)
    {
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > APP_RAM AT > APP_FLASH

    _sidata = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4)
    {
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > APP_RAM

    /* Stack at end of APP_RAM (grows down) */
    _stack_size = 4K;
    _stack_top = ORIGIN(APP_RAM) + LENGTH(APP_RAM);
    _stack_base = _stack_top - _stack_size;

    /* Heap between bss and stack */
    _heap_base = _ebss;
    _heap_size = _stack_base - _heap_base;
}
