ENTRY(_start)

SECTIONS {
    . = 0x20000;     /* separate from the application image at 0x10000 */

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
