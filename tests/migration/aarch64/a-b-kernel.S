#
# Copyright (c) 2018 Red Hat, Inc. and/or its affiliates
#
# Author:
#   Wei Huang <wei@redhat.com>
#
# This work is licensed under the terms of the GNU GPL, version 2 or later.
# See the COPYING file in the top-level directory.
#
# Note: Please make sure the compiler compiles the assembly code below with
# pc-relative address. Also the branch instructions should use relative
# addresses only.

#include "../migration-test.h"

.section .text

        .globl  _start

_start:
        /* disable MMU to use phys mem address */
        mrs     x0, sctlr_el1
        bic     x0, x0, #(1<<0)
        msr     sctlr_el1, x0
        isb

        /* traverse test memory region */
        mov     x0, #ARM_TEST_MEM_START
        mov     x1, #ARM_TEST_MEM_END

        /* output char 'A' to PL011 */
        mov     w3, 'A'
        mov     x2, #ARM_MACH_VIRT_UART
        strb    w3, [x2]

        /* clean up memory */
        mov     w3, #0
        mov     x4, x0
clean:
        strb    w3, [x4]
        add     x4, x4, #TEST_MEM_PAGE_SIZE
        cmp     x4, x1
        ble     clean

        /* w5 keeps a counter so we can limit the output speed */
        mov     w5, #0

        /* main body */
mainloop:
        mov     x4, x0

innerloop:
        /* increment the first byte of each page by 1 */
        ldrb    w3, [x4]
        add     w3, w3, #1
        and     w3, w3, #0xff
        strb    w3, [x4]

        /* make sure QEMU user space can see consistent data as MMU is off */
        dc      civac, x4

        add     x4, x4, #TEST_MEM_PAGE_SIZE
        cmp     x4, x1
        blt     innerloop

        add     w5, w5, #1
        and     w5, w5, #0xff
        cmp     w5, #0
        bne     mainloop

        /* output char 'B' to PL011 */
        mov     w3, 'B'
        strb    w3, [x2]

        b       mainloop
