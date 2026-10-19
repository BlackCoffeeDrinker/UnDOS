/*
 * Freestanding startup trampoline for userland applications built against
 * libundoscrt.
 *
 * The ELF loader (userland/elf_loader/loader.cpp) hands control to `_start`
 * with ESP pointing at a 16-byte-aligned entry frame:
 *
 *     esp+0: zero (reserved)
 *     esp+4: pointer to UnDOSStartupInfoV1
 *
 * The CRT owns initialization, main, and normal process cleanup.
 */
.text
.global _start
.type _start, @function
_start:
    cld
    movl 4(%esp), %eax
    subl $12, %esp
    pushl %eax
    call crt_start

    /* crt_exit never returns; hang here if it somehow does. */
.Lhang:
    hlt
    jmp .Lhang
.size _start, . - _start
.section .note.GNU-stack,"",@progbits
