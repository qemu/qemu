/*
 * Minimal start-up code for bare-metal R5900 tests
 *
 * The CPU starts in kernel mode at _start (PC set by the generic
 * loader device). Set up a stack, run main() and report its return
 * value to test_done(), where the gdb script is waiting.
 *
 * Author:
 *   Philippe Mathieu-Daudé
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

    .set    noreorder
    .section .text.boot, "ax"
    .globl  _start
    .ent    _start
_start:
    la      $sp, _stack_top
    jal     main
    nop
    jal     test_done
    move    $a0, $v0
1:  b       1b
    nop
    .end    _start
