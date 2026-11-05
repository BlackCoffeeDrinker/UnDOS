ENTRY(kernel_core_main)

SECTIONS
{
    . = 0xC0000000; /* Kernel Base VMA */

    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    /* Global constructor table walked once by kernel::crt (crt_init.cpp).
       The toolchain emits .ctors; .init_array is kept for other toolchains.
       Destructor tables (.dtors/.fini_array) are never run. */
    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))
        __init_array_end = .;
    }
    .bss : { *(.bss*) }
}
