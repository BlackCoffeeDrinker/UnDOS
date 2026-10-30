/*
 * This helper is deliberately limited to baseline i386 instructions before
 * the EFLAGS.ID test proves that CPUID can execute.
 */
.section .text,"ax",@progbits
.balign 4
.global udl_cpu_preflight
.type udl_cpu_preflight,@function
udl_cpu_preflight:
    pushl %ebx
    pushl %esi
    movl %eax, %esi
    testl %esi, %esi
    jz .Lunsupported

    pushfl
    popl %ecx
    testl $0x00020000, %ecx
    jnz .Lunsupported

    movw %cs, %dx
    testw $3, %dx
    jnz .Lunsupported
    testw $0xfffc, %dx
    jz .Lunsupported

    movw %ds, %dx
    testw $3, %dx
    jnz .Lunsupported
    testw $0xfffc, %dx
    jz .Lunsupported

    movw %ss, %dx
    testw $3, %dx
    jnz .Lunsupported
    testw $0xfffc, %dx
    jz .Lunsupported

    movl %cr0, %edx
    testl $0x00000001, %edx
    jz .Lunsupported
    testl $0x80000000, %edx
    jnz .Lunsupported

    movl $0, 0(%esi)
    movl $0, 4(%esi)
    movl $0, 8(%esi)

    pushfl
    popl %ecx
    movl %ecx, %edx
    xorl $0x00200000, %edx
    pushl %edx
    popfl
    pushfl
    popl %edx
    pushl %ecx
    popfl
    xorl %ecx, %edx
    testl $0x00200000, %edx
    jz .Lsupported

    movl $1, 0(%esi)
    xorl %eax, %eax
    cpuid
    movl %eax, 4(%esi)
    cmpl $1, %eax
    jb .Lsupported

    movl $1, %eax
    cpuid
    movl %edx, 8(%esi)

.Lsupported:
    xorl %eax, %eax
    popl %esi
    popl %ebx
    ret

.Lunsupported:
    movl $1, %eax
    popl %esi
    popl %ebx
    ret
.size udl_cpu_preflight, .-udl_cpu_preflight

.balign 4
.global udl_transfer_relocations
.type udl_transfer_relocations,@function
udl_transfer_relocations:
    cli
    jmp *%eax
.size udl_transfer_relocations, .-udl_transfer_relocations
