/* SPDX-License-Identifier: MIT */
/* UMI-OS Application Linker Script - Common Sections */
/* This file expects MEMORY regions to be defined by the including script. */

SECTIONS {
    /* ================================================================== */
    /* Application Header (must be first) */
    /* ================================================================== */

    .app_header : {
        KEEP(*(.app_header))
    } > APP_HDR

    /* ================================================================== */
    /* Code Section (.text) */
    /* ================================================================== */

    .text : {
        . = ALIGN(4);
        _stext = .;

        *(.text._start)     /* Entry point first */
        *(.text*)           /* Code */
        *(.glue_7)
        *(.glue_7t)

        . = ALIGN(4);
        _etext = .;
    } > APP_FLASH

    /* ================================================================== */
    /* Read-Only Data Section (.rodata) */
    /* ================================================================== */

    .rodata : {
        . = ALIGN(4);
        _srodata = .;

        *(.rodata*)

        . = ALIGN(4);
        _erodata = .;
    } > APP_FLASH

    /* ================================================================== */
    /* ARM Exception Tables */
    /* ================================================================== */

    .ARM.extab : {
        *(.ARM.extab*)
    } > APP_FLASH

    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > APP_FLASH

    /* ================================================================== */
    /* Init/Fini Arrays (constructors/destructors) */
    /* ================================================================== */

    .init_array : {
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(.init_array*))
        KEEP(*(SORT(.init_array.*)))
        __init_array_end = .;
    } > APP_FLASH

    .fini_array : {
        . = ALIGN(4);
        __fini_array_start = .;
        KEEP(*(.fini_array*))
        KEEP(*(SORT(.fini_array.*)))
        __fini_array_end = .;
    } > APP_FLASH

    /* End of flash content */
    _sidata = LOADADDR(.data);

    /* ================================================================== */
    /* Initialized Data Section (.data) */
    /* ================================================================== */

    .data : {
        . = ALIGN(4);
        _sdata = .;

        *(.data*)

        . = ALIGN(4);
        _edata = .;
    } > APP_DATA AT> APP_FLASH

    /* ================================================================== */
    /* Uninitialized Data Section (.bss) */
    /* ================================================================== */

    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;

        *(.bss*)
        *(COMMON)

        . = ALIGN(4);
        _ebss = .;
    } > APP_DATA

    /* ================================================================== */
    /* Stack / Heap */
    /* ================================================================== */

    /* Stack grows downward from end of APP_STACK */
    .stack (NOLOAD) : {
        . = ALIGN(8);
        _sstack = ORIGIN(APP_STACK);
        _sheap = ORIGIN(APP_STACK);
        _eheap = ORIGIN(APP_STACK) + LENGTH(APP_STACK);
        _estack = ORIGIN(APP_STACK) + LENGTH(APP_STACK);
    } > APP_STACK

    /* ================================================================== */
    /* Size Calculations (for header) */
    /* ================================================================== */

    _text_size = _etext - _stext;
    _rodata_size = _erodata - _srodata;
    _data_size = _edata - _sdata;
    _bss_size = _ebss - _sbss;

    /* ================================================================== */
    /* Discard */
    /* ================================================================== */

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
    }
}

/* Provide default symbols if not defined */
PROVIDE(__init_array_start = 0);
PROVIDE(__init_array_end = 0);
PROVIDE(__fini_array_start = 0);
PROVIDE(__fini_array_end = 0);
