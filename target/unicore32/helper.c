/*
 * Copyright (C) 2010-2012 Guan Xuetao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Contributions from 2012-04-01 on are considered under GPL version 2,
 * or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "hw/semihosting/console.h"

#undef DEBUG_UC32

#ifdef DEBUG_UC32
#define DPRINTF(fmt, ...) printf("%s: " fmt , __func__, ## __VA_ARGS__)
#else
#define DPRINTF(fmt, ...) do {} while (0)
#endif

#ifndef CONFIG_USER_ONLY
void helper_cp0_set(CPUUniCore32State *env, uint32_t val, uint32_t creg,
        uint32_t cop)
{
    /*
     * movc pp.nn, rn, #imm9
     *      rn: UCOP_REG_D
     *      nn: UCOP_REG_N
     *          1: sys control reg.
     *          2: page table base reg.
     *          3: data fault status reg.
     *          4: insn fault status reg.
     *          5: cache op. reg.
     *          6: tlb op. reg.
     *      imm9: split UCOP_IMM10 with bit5 is 0
     */
    switch (creg) {
    case 1:
        if (cop != 0) {
            goto unrecognized;
        }
        env->cp0.c1_sys = val;
        break;
    case 2:
        if (cop != 0) {
            goto unrecognized;
        }
        env->cp0.c2_base = val;
        break;
    case 3:
        if (cop != 0) {
            goto unrecognized;
        }
        env->cp0.c3_faultstatus = val;
        break;
    case 4:
        if (cop != 0) {
            goto unrecognized;
        }
        env->cp0.c4_faultaddr = val;
        break;
    case 5:
        switch (cop) {
        case 28:
            DPRINTF("Invalidate Entire I&D cache\n");
            return;
        case 20:
            DPRINTF("Invalidate Entire Icache\n");
            return;
        case 12:
            DPRINTF("Invalidate Entire Dcache\n");
            return;
        case 10:
            DPRINTF("Clean Entire Dcache\n");
            return;
        case 14:
            DPRINTF("Flush Entire Dcache\n");
            return;
        case 13:
            DPRINTF("Invalidate Dcache line\n");
            return;
        case 11:
            DPRINTF("Clean Dcache line\n");
            return;
        case 15:
            DPRINTF("Flush Dcache line\n");
            return;
        }
        break;
    case 6:
        if ((cop <= 6) && (cop >= 2)) {
            /* invalid all tlb */
            tlb_flush(env_cpu(env));
            return;
        }
        break;
    default:
        goto unrecognized;
    }
    return;
unrecognized:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "Wrong register (%d) or wrong operation (%d) in cp0_set!\n",
                  creg, cop);
}

uint32_t helper_cp0_get(CPUUniCore32State *env, uint32_t creg, uint32_t cop)
{
    /*
     * movc rd, pp.nn, #imm9
     *      rd: UCOP_REG_D
     *      nn: UCOP_REG_N
     *          0: cpuid and cachetype
     *          1: sys control reg.
     *          2: page table base reg.
     *          3: data fault status reg.
     *          4: insn fault status reg.
     *      imm9: split UCOP_IMM10 with bit5 is 0
     */
    switch (creg) {
    case 0:
        switch (cop) {
        case 0:
            return env->cp0.c0_cpuid;
        case 1:
            return env->cp0.c0_cachetype;
        }
        break;
    case 1:
        if (cop == 0) {
            return env->cp0.c1_sys;
        }
        break;
    case 2:
        if (cop == 0) {
            return env->cp0.c2_base;
        }
        break;
    case 3:
        if (cop == 0) {
            return env->cp0.c3_faultstatus;
        }
        break;
    case 4:
        if (cop == 0) {
            return env->cp0.c4_faultaddr;
        }
        break;
    }
    qemu_log_mask(LOG_GUEST_ERROR,
                  "Wrong register (%d) or wrong operation (%d) in cp0_set!\n",
                  creg, cop);
    return 0;
}

void helper_cp1_putc(target_ulong regval)
{
    const char c = regval;

    qemu_semihosting_log_out(&c, sizeof(c));
}
#endif /* !CONFIG_USER_ONLY */

bool uc32_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        UniCore32CPU *cpu = UNICORE32_CPU(cs);
        CPUUniCore32State *env = &cpu->env;

        if (!(env->uncached_asr & ASR_I)) {
            cs->exception_index = UC32_EXCP_INTR;
            uc32_cpu_do_interrupt(cs);
            return true;
        }
    }
    return false;
}
