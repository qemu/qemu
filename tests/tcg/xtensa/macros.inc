#include "core-isa.h"

.macro test_suite name
.data
status: .word result
result: .space 256
.text
.global main
.align 4
main:
.endm

.macro reset_ps
    movi    a2, 0x4000f
    wsr     a2, ps
    isync
.endm

.macro test_suite_end
    reset_ps
    movi    a0, status
    l32i    a2, a0, 0
    movi    a0, result
    sub     a2, a2, a0
    movi    a3, 0
    loopnez a2, 1f
    l8ui    a2, a0, 0
    or      a3, a3, a2
    addi    a0, a0, 1
1:
    exit
.endm

.macro print text
.data
97: .ascii "\text\n"
98:
    .align 4
.text
    movi    a2, 4
    movi    a3, 2
    movi    a4, 97b
    movi    a5, 98b
    sub     a5, a5, a4
    simcall
.endm

.macro test_init
.endm

.macro test name
    //print test_\name
    test_init
test_\name:
.global test_\name
.endm

.macro test_end
99:
    reset_ps
    movi    a2, status
    l32i    a3, a2, 0
    addi    a3, a3, 1
    s32i    a3, a2, 0
.endm

.macro exit
    movi    a2, 1
    simcall
.endm

.macro test_fail
    movi    a2, status
    l32i    a2, a2, 0
    movi    a3, 1
    s8i     a3, a2, 0
    j       99f
.endm

.macro assert cond, arg1, arg2
    b\cond  \arg1, \arg2, 90f
    test_fail
90:
    nop
.endm

.macro set_vector vector, addr
    movi    a2, handler_\vector
    movi    a3, \addr
    s32i    a3, a2, 0
.endm
