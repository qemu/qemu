/* Purpose: test example, verify the soundness of the jump operation */

#define SYS_exit_group 94

    .text
    .globl _start

_start:
    {
        jump pass
    }
    /*
     * Inlined fail label in crt.S so we can fail without
     * having a functioning jump
     */
    {
        r0 = #1
        r6 = #SYS_exit_group
    }
    {
        trap0(#1)
    }
