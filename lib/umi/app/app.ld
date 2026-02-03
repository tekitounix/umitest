/* SPDX-License-Identifier: MIT */
/* UMI-OS Application Linker Script */
/* For .umia binaries loaded by kernel */

/* Memory regions - must match kernel's APP_FLASH address */
MEMORY {
    /* .umia header lives at the start of APP_FLASH in kernel.ld */
    APP_HDR   (rx)  : ORIGIN = 0x08060000, LENGTH = 128   /* AppHeader (128 bytes) */
    /* Application runs XIP after header */
    APP_FLASH (rx)  : ORIGIN = 0x08060080, LENGTH = (128K - 128)
    /* Application RAM (matches kernel APP_RAM split) */
    APP_DATA  (rwx) : ORIGIN = 0x2000C000, LENGTH = 32K   /* .data/.bss */
    APP_STACK (rwx) : ORIGIN = 0x20014000, LENGTH = 16K   /* Heap + Stack */
}

/* Entry point */
ENTRY(_start)

INCLUDE app_sections.ld
