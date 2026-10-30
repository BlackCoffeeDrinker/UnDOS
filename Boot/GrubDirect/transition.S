.section .text.udl_transition,"ax",@progbits
#include "transition_layout.h"
.balign 16
.global udl_transition_start
.global udl_transition_after_far
.global udl_transition_end
.global udl_raw_gate_start
.global udl_raw_gate_end
.type udl_transition_start,@function

.macro UDL_DIAG_CHECK stop, stage
    movl %ecx, %eax
    movl UDL_TRANSITION_DIAG_STOP_OFFSET(%edi), %ecx
    leal -\stop(%ecx), %ecx
    jecxz 1f
    movl %eax, %ecx
    leal UDL_TRANSITION_PARAMETERS_DELTA(%edi), %eax
    jmp 2f
1:
    movl $\stage, %esi
    jmp udl_transition_diag_hold
2:
.endm

.macro UDL_VGA_FIXED offset, value
    movw $(0x4f00 + \value), (0x000b8f00 + (2 * \offset))
.endm

.macro UDL_VGA_NEXT value
    movb $\value, (%edi)
    movb $0x4f, 1(%edi)
    addl $2, %edi
.endm

udl_transition_start:
    cli
    cld
    leal -UDL_TRANSITION_PARAMETERS_DELTA(%eax), %edi
    UDL_DIAG_CHECK 7, 6
    lgdt 0(%edi)
    ljmp *6(%edi)

udl_transition_after_far:
    movw $0x18, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl 16(%edi), %esp
    movl %esp, %ebp
    UDL_DIAG_CHECK 8, 7
    movl UDL_TRANSITION_CLEANUP_MASK_OFFSET(%edi), %eax
    testl %eax, %eax
    jz 2f
    movl %cr4, %ecx
    notl %eax
    andl %eax, %ecx
    movl %ecx, %cr4
2:
    UDL_DIAG_CHECK 9, 8
    movl 12(%edi), %eax
    movl %eax, %cr3
    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0
    jmp 1f
1:
    UDL_DIAG_CHECK 10, 9
    movl 20(%edi), %esp
    movl %esp, %ebp
    andl $-16, %esp
    subl $12, %esp
    pushl 24(%edi)
    pushl $0
    movl 28(%edi), %esi
    xorl %ebx, %ebx
    UDL_DIAG_CHECK 11, 10
    movw $0x3f9, %dx
    xorl %eax, %eax
    outb %al, %dx
    movw $0x3fb, %dx
    movb $0x80, %al
    outb %al, %dx
    movw $0x3f8, %dx
    movb $1, %al
    outb %al, %dx
    movw $0x3f9, %dx
    xorl %eax, %eax
    outb %al, %dx
    movw $0x3fb, %dx
    movb $3, %al
    outb %al, %dx
    movw $0x3fa, %dx
    movb $0xc7, %al
    outb %al, %dx
    movw $0x3fc, %dx
    movb $0x0b, %al
    outb %al, %dx
    movw $0x3f8, %dx
    movb $'U', %al
    outb %al, %dx
    movb $'D', %al
    outb %al, %dx
    movb $'L', %al
    outb %al, %dx
    movb $':', %al
    outb %al, %dx
    movb $'s', %al
    outb %al, %dx
    movb $'t', %al
    outb %al, %dx
    movb $'u', %al
    outb %al, %dx
    movb $'b', %al
    outb %al, %dx
    movb $'\n', %al
    outb %al, %dx
    UDL_DIAG_CHECK 12, 11
    movl %esi, %eax
    xorl %esi, %esi
    xorl %edi, %edi
    jmp *%eax

.type udl_transition_diag_hold,@function
udl_transition_diag_hold:
    cli
    movl %edi, %ebp
    movl $0x000b8f00, %edi
    movl $80, %ecx
    movw $0x4f20, %dx
1:
    movw %dx, (%edi)
    addl $2, %edi
    loop 1b

    movl $0x000b8f00, %edi
    UDL_VGA_FIXED 0, 'U'
    UDL_VGA_FIXED 1, 'D'
    UDL_VGA_FIXED 2, 'L'
    UDL_VGA_FIXED 3, ' '
    UDL_VGA_FIXED 4, 'D'
    UDL_VGA_FIXED 5, 'I'
    UDL_VGA_FIXED 6, 'A'
    UDL_VGA_FIXED 7, 'G'
    UDL_VGA_FIXED 8, ' '
    leal 18(%edi), %edi
    movl UDL_TRANSITION_DIAG_TEXT_OFFSET(%ebp), %ebx
    shll $4, %esi
    addl %ebx, %esi
    movl $16, %ecx
2:
    movzbl (%esi), %eax
    testb %al, %al
    jz 3f
    orw $0x4f00, %ax
    movw %ax, (%edi)
    addl $2, %edi
    incl %esi
    loop 2b
3:
    UDL_VGA_NEXT 'S'
    UDL_VGA_NEXT 'T'
    UDL_VGA_NEXT 'O'
    UDL_VGA_NEXT 'P'
    UDL_VGA_NEXT '='
    movl UDL_TRANSITION_DIAG_TEXT_OFFSET(%ebp), %ebx
    movl UDL_TRANSITION_DIAG_STOP_OFFSET(%ebp), %esi
    decl %esi
    shll $4, %esi
    addl %ebx, %esi
    movl $16, %ecx
4:
    movzbl (%esi), %eax
    testb %al, %al
    jz 5f
    orw $0x4f00, %ax
    movw %ax, (%edi)
    addl $2, %edi
    incl %esi
    loop 4b
5:
6:
    jmp 6b

udl_transition_end:
.size udl_transition_start, udl_transition_end-udl_transition_start

.section .text.udl_raw_gate,"ax",@progbits
.type udl_raw_gate_start,@function
udl_raw_gate_start:
    UDL_VGA_FIXED 0, 'U'
    UDL_VGA_FIXED 1, 'D'
    UDL_VGA_FIXED 2, 'L'
    UDL_VGA_FIXED 3, ' '
    UDL_VGA_FIXED 4, 'D'
    UDL_VGA_FIXED 5, 'I'
    UDL_VGA_FIXED 6, 'A'
    UDL_VGA_FIXED 7, 'G'
    UDL_VGA_FIXED 8, ' '
    UDL_VGA_FIXED 9, 'c'
    UDL_VGA_FIXED 10, 'o'
    UDL_VGA_FIXED 11, 'p'
    UDL_VGA_FIXED 12, 'i'
    UDL_VGA_FIXED 13, 'e'
    UDL_VGA_FIXED 14, 'd'
    UDL_VGA_FIXED 15, ' '
    UDL_VGA_FIXED 16, 'S'
    UDL_VGA_FIXED 17, 'T'
    UDL_VGA_FIXED 18, 'O'
    UDL_VGA_FIXED 19, 'P'
    UDL_VGA_FIXED 20, '='
    UDL_VGA_FIXED 21, 'c'
    UDL_VGA_FIXED 22, 'o'
    UDL_VGA_FIXED 23, 'p'
    UDL_VGA_FIXED 24, 'i'
    UDL_VGA_FIXED 25, 'e'
    UDL_VGA_FIXED 26, 'd'
    UDL_VGA_FIXED 27, ' '
    UDL_VGA_FIXED 28, 'R'
    UDL_VGA_FIXED 29, 'E'
    UDL_VGA_FIXED 30, 'S'
    UDL_VGA_FIXED 31, 'E'
    UDL_VGA_FIXED 32, 'T'
    UDL_VGA_FIXED 33, ' '
    UDL_VGA_FIXED 34, 'A'
    UDL_VGA_FIXED 35, 'F'
    UDL_VGA_FIXED 36, 'T'
    UDL_VGA_FIXED 37, 'E'
    UDL_VGA_FIXED 38, 'R'
    UDL_VGA_FIXED 39, ' '
    UDL_VGA_FIXED 40, 'O'
    UDL_VGA_FIXED 41, 'B'
    UDL_VGA_FIXED 42, 'S'
    UDL_VGA_FIXED 43, 'E'
    UDL_VGA_FIXED 44, 'R'
    UDL_VGA_FIXED 45, 'V'
    UDL_VGA_FIXED 46, 'A'
    UDL_VGA_FIXED 47, 'T'
    UDL_VGA_FIXED 48, 'I'
    UDL_VGA_FIXED 49, 'O'
    UDL_VGA_FIXED 50, 'N'
    cli
7:
    jmp 7b
udl_raw_gate_end:
.size udl_raw_gate_start, udl_raw_gate_end-udl_raw_gate_start
