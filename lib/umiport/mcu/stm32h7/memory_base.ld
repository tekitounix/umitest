/* STM32H750 internal memory map (MCU固有) */
/* Board-level linker scripts should INCLUDE this file */
/* and add external memory definitions (SDRAM, QSPI, etc.) */

/* Internal memories */
FLASH       (rx)  : ORIGIN = 0x08000000, LENGTH = 128K   /* Internal Flash */
DTCMRAM     (rwx) : ORIGIN = 0x20000000, LENGTH = 128K   /* DTCM (fast, no DMA) */
ITCMRAM     (rwx) : ORIGIN = 0x00000000, LENGTH = 64K    /* ITCM (instruction) */
SRAM_D1     (rwx) : ORIGIN = 0x24000000, LENGTH = 512K   /* AXI SRAM (D1 domain) */
SRAM_D2     (rwx) : ORIGIN = 0x30000000, LENGTH = 288K   /* SRAM1+2+3 (D2 domain) */
SRAM_D3     (rwx) : ORIGIN = 0x38000000, LENGTH = 64K    /* SRAM4 (D3 domain) */
BACKUP_SRAM (rwx) : ORIGIN = 0x38800000, LENGTH = 4K     /* Backup SRAM */
