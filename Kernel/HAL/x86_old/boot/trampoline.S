.global entry_trampoline
.type entry_trampoline, @function

entry_trampoline:
    # Stage 1.5 calls this function using standard cdecl convention.
    # Stack layout at this moment:
    # [esp + 12] -> high_half_stack_top
    # [esp + 8]  -> boot_info_vaddr
    # [esp + 4]  -> kernel_entry_vaddr (0xc0000088)
    # [esp]      -> Return address back to Stage 1.5 C code (which we will discard)

    mov 4(%esp), %eax   # eax = kernel entry pointer
    mov 8(%esp), %ecx   # ecx = boot_info virtual address
    mov 12(%esp), %edx  # edx = high-half stack top destination

    # Step 1: Clear interrupts out of absolute caution
    cli

    # Step 2: Leap onto the high-half kernel stack
    mov %edx, %esp
    mov %esp, %ebp

    # Step 3: Enforce strict 16-byte System V ABI alignment
    and $-16, %esp       # Force ESP to a 16-byte boundary (e.g., 0xXXXXXX20)
    sub $12, %esp        # Create padding headroom (ESP becomes 0xXXXXXX14)
    push %ecx            # Push boot_info pointer (Arg 1) -> (ESP becomes 0xXXXXXX10)
    push $0              # Push a fake 4-byte Return Address -> (ESP becomes 0xXXXXXX0C)

    # Verification check:
    # Inside kernel_core_main, (ESP + 4) will equal 0xXXXXXX10, which is perfectly 16-byte aligned.
    # The C++ function will look for its argument at [esp + 4] and find the boot_info reference flawlessly.

    # Step 4: Flush CPU registers to prevent leaking Stage 1.5 junk values
    xor %ebx, %ebx
    xor %edi, %edi
    xor %esi, %esi

    # Step 5: Execute the high-half jump
    jmp *%eax

# Safety net if the kernel somehow returns
halt_loop:
    hlt
    jmp halt_loop
