/* Daisy Seed memory map */
/* MCU内蔵メモリ + ボード固有の外部メモリ */

MEMORY
{
    /* STM32H750 internal (from mcu/stm32h7/memory_base.ld) */
    FLASH       (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
    DTCMRAM     (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    ITCMRAM     (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
    SRAM_D1     (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
    SRAM_D2     (rwx) : ORIGIN = 0x30000000, LENGTH = 288K
    SRAM_D3     (rwx) : ORIGIN = 0x38000000, LENGTH = 64K
    BACKUP_SRAM (rwx) : ORIGIN = 0x38800000, LENGTH = 4K

    /* Daisy Seed external memories */
    SDRAM       (rwx) : ORIGIN = 0xC0000000, LENGTH = 64M
    QSPIFLASH   (rx)  : ORIGIN = 0x90000000, LENGTH = 8M
}

/* DMA buffer region: first 32K of SRAM_D2, non-cacheable via MPU */
_dma_buffer_start = 0x30000000;
_dma_buffer_size  = 0x8000;  /* 32KB */
