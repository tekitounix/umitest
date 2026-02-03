/* Linker script for STM32CubeProgrammer External Loader
 * Target: STM32H750VBT6 (Daisy Seed)
 *
 * StorageInfo must be at address 0x00000000 (CubeProgrammer requirement).
 * Code runs from SRAM (loaded by CubeProgrammer at runtime).
 */

MEMORY
{
    DEV_INFO (r)  : ORIGIN = 0x00000000, LENGTH = 256
    RAM      (xrw) : ORIGIN = 0x20000004, LENGTH = 128K - 4
}

ENTRY(Init)

SECTIONS
{
    .dev_info :
    {
        KEEP(*Dev_Inf*.o(.rodata* .data*))
    } > DEV_INFO

    .text :
    {
        . = ALIGN(4);
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > RAM

    .data :
    {
        . = ALIGN(4);
        *(.data*)
        . = ALIGN(4);
    } > RAM

    .bss :
    {
        . = ALIGN(4);
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
    } > RAM
}
