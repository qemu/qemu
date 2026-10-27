/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdint.h>
#include <stdio.h>
#include <setjmp.h>
#include <signal.h>

uint32_t err;

#include "hex_test.h"

#define VSP_INITIAL_OFFSET (5 * 1024)
#define VALLOCFRAME_VALUE 2
#define VSP_AFTER_VALLOCFRAME \
    (VSP_INITIAL_OFFSET - (VALLOCFRAME_VALUE * 2 * 1024))

#define VALLOCFRAME_IMM ".word 0x6080c802\n" /* vallocframe(#2) */
#define VALLOCFRAME_REG ".word 0x6080d000\n" /* vallocframe(r0) */
#define VDEALLOCFRAME_IMM ".word 0x6a80c802\n" /* vdeallocframe(#2) */
#define VDEALLOCFRAME_REG ".word 0x6940d000\n" /* vdeallocframe(r0) */

static uint32_t read_vsp(void)
{
    uint32_t value;

    asm volatile(".word 0x6a1dc000\n" /* r0 = vsp */
                 "%0 = r0\n"
                 : "=r"(value)
                 :
                 : "r0");
    return value;
}

static void write_vsp(uint32_t value)
{
    asm volatile("r0 = %0\n"
                 ".word 0x6220c01d\n" /* vsp = r0 */
                 :
                 : "r"(value)
                 : "r0");
}

static void write_vframelimit(uint32_t value)
{
    asm volatile("r0 = %0\n"
                 ".word 0x6220c01c\n" /* vframelimit = r0 */
                 :
                 : "r"(value)
                 : "r0");
}

static uint32_t __attribute__((noinline)) keep(uint32_t a, uint32_t b)
{
    write_vsp(b);
    return a * 3;
}

static sigjmp_buf overflow_jmp;
static int overflow_sig;
static int overflow_code;

static void overflow_handler(int sig, siginfo_t *info, void *context)
{
    overflow_sig = sig;
    overflow_code = info->si_code;
    siglongjmp(overflow_jmp, 1);
}

static void test_vallocframe_imm(void)
{
    write_vsp(VSP_INITIAL_OFFSET);
    write_vframelimit(0);
    asm volatile(VALLOCFRAME_IMM);
    check32(VSP_AFTER_VALLOCFRAME, read_vsp());
    asm volatile(VDEALLOCFRAME_IMM);
    check32(VSP_INITIAL_OFFSET, read_vsp());
}

static void test_vallocframe_reg(void)
{
    uint32_t value = VALLOCFRAME_VALUE;

    write_vsp(VSP_INITIAL_OFFSET);
    write_vframelimit(0);
    asm volatile("r0 = %0\n" VALLOCFRAME_REG : : "r"(value) : "r0");
    check32(VSP_AFTER_VALLOCFRAME, read_vsp());
    asm volatile("r0 = %0\n" VDEALLOCFRAME_REG : : "r"(value) : "r0");
    check32(VSP_INITIAL_OFFSET, read_vsp());
}

static void test_write_vsp_clobber(void)
{
    /* Prevent constant folding so a remains live across write_vsp(). */
    static volatile uint32_t x = 111, y = 222;

    check32(keep(x, y), 333);
}

static void test_vallocframe_overflow(void)
{
    struct sigaction action = {
        .sa_sigaction = overflow_handler,
        .sa_flags = SA_SIGINFO,
    };

    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, NULL);
    if (sigsetjmp(overflow_jmp, 1) == 0) {
        write_vsp(VSP_INITIAL_OFFSET);
        write_vframelimit(VSP_AFTER_VALLOCFRAME + 1);
        asm volatile(VALLOCFRAME_IMM);
        check32(1, 0);
    }
    check32(SIGSEGV, overflow_sig);
    check32(SEGV_BNDERR, overflow_code);
}

int main(void)
{
    test_write_vsp_clobber();
    test_vallocframe_imm();
    test_vallocframe_reg();
    test_vallocframe_overflow();
    puts(err ? "FAIL" : "PASS");
    return err;
}
