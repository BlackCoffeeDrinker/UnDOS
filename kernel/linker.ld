ENTRY(kernel_main)


SECTIONS
{
    . = 0xC0000000; /* Kernel Base VMA */

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}
