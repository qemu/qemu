/*
 *  Startup Code for MIPS64 CPU-core
 *
 */
.text
.globl _start
.align 4
_start:
    ori    $2, $2, 0xffff
    sll    $2, $2, 16
    ori    $2, $2, 0xffff
    mtc0   $2, $12, 0
    jal    main

end:
    b end
