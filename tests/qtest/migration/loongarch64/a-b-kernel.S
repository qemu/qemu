/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "../bootfile.h"

#define LOONGARCH_CSR_CRMD          0
#define LOONGARCH_VIRT_UART         0x1FE001E0
.section .text

    .globl  _start
_start:
    /* output char 'A' to UART16550 */
    li.d    $t0, LOONGARCH_VIRT_UART
    li.w    $t1, 'A'
    st.b    $t1, $t0, 0

    /* traverse test memory region */
    li.d    $t0, LOONGARCH_TEST_MEM_START
    li.d    $t1, LOONGARCH_TEST_MEM_END
    li.d    $t2, TEST_MEM_PAGE_SIZE
    li.d    $t4, LOONGARCH_VIRT_UART
    li.w    $t5, 'B'

clean:
    st.b    $zero, $t0, 0
    add.d   $t0,   $t0, $t2
    bne     $t0,   $t1, clean
    /* keeps a counter so we can limit the output speed */
    addi.d  $t6,   $zero, 0

mainloop:
    li.d    $t0, LOONGARCH_TEST_MEM_START

innerloop:
    ld.bu   $t3, $t0, 0
    addi.w  $t3, $t3, 1
    ext.w.b $t3, $t3
    st.b    $t3, $t0, 0
    add.d   $t0, $t0, $t2
    bne     $t0, $t1, innerloop

    addi.d  $t6, $t6, 1
    andi    $t6, $t6, 31
    bnez    $t6, mainloop

    st.b    $t5, $t4, 0
    b       mainloop
