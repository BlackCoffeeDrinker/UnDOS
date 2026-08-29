/*
 * Freestanding startup trampoline for userland applications built against
 * libundoscrt.
 *
 * The ELF loader (userland/elf_loader/loader.cpp) hands control to `_start`
 * with ESP pointing at a small block it built on the new process's stack:
 *
 *     esp+0: argc   (uint32_t)
 *     esp+4: argv   (pointer to argc+1 char* entries, NULL-terminated)
 *
 * `_start` grabs both words before doing anything else that could touch the
 * stack, calls the application's `main(argc, argv)`, and terminates the
 * process with its return value via the PROC Exit syscall -- there is no
 * caller to return to.
 */
.text
.global _start
.type _start, @function
_start:
    movl (%esp), %eax      /* argc */
    movl 4(%esp), %ebx     /* argv */

    pushl %ebx
    pushl %eax
    call main

    /* main's return value (eax) becomes the process exit code. */
    pushl %eax
    call crt_exit

    /* crt_exit never returns; hang here if it somehow does. */
.Lhang:
    hlt
    jmp .Lhang
.size _start, . - _start
