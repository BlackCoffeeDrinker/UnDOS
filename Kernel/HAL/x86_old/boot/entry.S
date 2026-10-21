.section .multiboot, "a"
.align 4

.set MULTIBOOT_MAGIC,    0x1BADB002
.set MULTIBOOT_FLAGS,    0x00000003  # ALIGN + MEMINFO
.set MULTIBOOT_CHECKSUM, -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

.long MULTIBOOT_MAGIC
.long MULTIBOOT_FLAGS
.long MULTIBOOT_CHECKSUM


.section .boot, "ax"
.global _start
.type _start, @function

_start:
    cli

    # --- Early VGA Debug: Print "UnDOS" to top-left of screen ---
    mov $0xB8000, %edx
    movw $0x0F55, 0(%edx)    # 'U', Bright White (0x0F)
    movw $0x0F6E, 2(%edx)    # 'n'
    movw $0x0F44, 4(%edx)    # 'D'
    movw $0x0F4F, 6(%edx)    # 'O'
    movw $0x0F53, 8(%edx)    # 'S'
    # ------------------------------------------------------------
    # Set up a temporary boot stack for Stage 1.5 execution
    mov $stage15_stack_top, %esp
    mov %esp, %ebp

    # Push the physical address of the Multiboot Info structure onto the stack
    push %ebx
    
    # Leap to C++ logic
    call kernel_main

.hang:
    hlt
    jmp .hang

.section .bss
.align 16
stage15_stack_bottom:
    .skip 16384  # 16KB execution stack
stage15_stack_top:
