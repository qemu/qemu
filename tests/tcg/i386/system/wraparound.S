/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Regression test for the translator_ld crash when an instruction
 * straddles the end of the 32-bit address space (i386).
 */

        .code32
        .section .text

main:
        /*
         * The top page (0xfffff000) is SeaBIOS ROM and cannot be written.
         * Its byte at 0xffffffff (0x00 = "add r/m8, r8") already crosses the
         * page boundary into page1 at 0x0, which is exactly the case
         * translator_ld must handle without aborting.  Reaching 0xfffffffe
         * runs the ROM's cld, then that add; the add's modrm is fetched from
         * [0x0], which is RAM, so build a short exit stub there:
         *
         *   [0x0] c0                 modrm -> "add al, al" (reg; EIP -> 1)
         *   [0x1] c3                 ret
         *
         * Note: this relies on the SeaBIOS byte at 0xffffffff being 0x00
         * (add r/m8, r8); if that ever changes, the stub below must move.
         *
         * Note that eax = 0 before and after the stub, so this becomes
         * the exit code of the test.
         */
        xor     %eax, %eax
        movw    $0xc3c0, (%eax)
        movl    $0xfffffffe, %ecx
        jmp     *%ecx

        .globl  main
        .type   main, @function
        .size   main, . - main
