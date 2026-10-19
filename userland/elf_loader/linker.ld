ENTRY(_start)

SECTIONS {
    /* Leave room for application growth below the shared-library window. */
    . = 0x08000000;
    __loader_image_start = .;

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

    __loader_image_end = ALIGN(4096);
    ASSERT(__loader_image_end <= 0x10000000, "ELoader overlaps shared-library window")
}
