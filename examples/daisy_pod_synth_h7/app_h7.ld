/* SPDX-License-Identifier: MIT */
/* UMI-OS Application Linker Script — Daisy Seed H750 QSPI XIP */
/* .umia binary loaded by kernel from QSPI flash at 0x90000000 */

MEMORY {
    APP_HDR   (rx)  : ORIGIN = 0x90000000, LENGTH = 128         /* AppHeader */
    APP_FLASH (rx)  : ORIGIN = 0x90000080, LENGTH = (8M - 128)  /* XIP code */
    APP_DATA  (rwx) : ORIGIN = 0x24040000, LENGTH = 160K        /* .data/.bss */
    APP_STACK (rwx) : ORIGIN = 0x24068000, LENGTH = 32K         /* Heap + Stack */
}

ENTRY(_start)

INCLUDE app_sections.ld
