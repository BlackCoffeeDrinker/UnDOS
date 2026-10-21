ENTRY(HAL_PLATFORM_Init) /* Ensure the entry point is explicitly designated */
SECTIONS
{
    . = 0x80000000;     /* HAL Base VMA */

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}
