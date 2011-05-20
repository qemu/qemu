/*
 *  Alpha emulation cpu helpers for qemu.
 *
 *  Copyright (c) 2007 Jocelyn Mayer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "cpu.h"
#include "exec-all.h"
#include "softfloat.h"

uint64_t cpu_alpha_load_fpcr (CPUState *env)
{
    uint64_t r = 0;
    uint8_t t;

    t = env->fpcr_exc_status;
    if (t) {
        r = FPCR_SUM;
        if (t & float_flag_invalid) {
            r |= FPCR_INV;
        }
        if (t & float_flag_divbyzero) {
            r |= FPCR_DZE;
        }
        if (t & float_flag_overflow) {
            r |= FPCR_OVF;
        }
        if (t & float_flag_underflow) {
            r |= FPCR_UNF;
        }
        if (t & float_flag_inexact) {
            r |= FPCR_INE;
        }
    }

    t = env->fpcr_exc_mask;
    if (t & float_flag_invalid) {
        r |= FPCR_INVD;
    }
    if (t & float_flag_divbyzero) {
        r |= FPCR_DZED;
    }
    if (t & float_flag_overflow) {
        r |= FPCR_OVFD;
    }
    if (t & float_flag_underflow) {
        r |= FPCR_UNFD;
    }
    if (t & float_flag_inexact) {
        r |= FPCR_INED;
    }

    switch (env->fpcr_dyn_round) {
    case float_round_nearest_even:
        r |= FPCR_DYN_NORMAL;
        break;
    case float_round_down:
        r |= FPCR_DYN_MINUS;
        break;
    case float_round_up:
        r |= FPCR_DYN_PLUS;
        break;
    case float_round_to_zero:
        r |= FPCR_DYN_CHOPPED;
        break;
    }

    if (env->fpcr_dnz) {
        r |= FPCR_DNZ;
    }
    if (env->fpcr_dnod) {
        r |= FPCR_DNOD;
    }
    if (env->fpcr_undz) {
        r |= FPCR_UNDZ;
    }

    return r;
}

void cpu_alpha_store_fpcr (CPUState *env, uint64_t val)
{
    uint8_t t;

    t = 0;
    if (val & FPCR_INV) {
        t |= float_flag_invalid;
    }
    if (val & FPCR_DZE) {
        t |= float_flag_divbyzero;
    }
    if (val & FPCR_OVF) {
        t |= float_flag_overflow;
    }
    if (val & FPCR_UNF) {
        t |= float_flag_underflow;
    }
    if (val & FPCR_INE) {
        t |= float_flag_inexact;
    }
    env->fpcr_exc_status = t;

    t = 0;
    if (val & FPCR_INVD) {
        t |= float_flag_invalid;
    }
    if (val & FPCR_DZED) {
        t |= float_flag_divbyzero;
    }
    if (val & FPCR_OVFD) {
        t |= float_flag_overflow;
    }
    if (val & FPCR_UNFD) {
        t |= float_flag_underflow;
    }
    if (val & FPCR_INED) {
        t |= float_flag_inexact;
    }
    env->fpcr_exc_mask = t;

    switch (val & FPCR_DYN_MASK) {
    case FPCR_DYN_CHOPPED:
        t = float_round_to_zero;
        break;
    case FPCR_DYN_MINUS:
        t = float_round_down;
        break;
    case FPCR_DYN_NORMAL:
        t = float_round_nearest_even;
        break;
    case FPCR_DYN_PLUS:
        t = float_round_up;
        break;
    }
    env->fpcr_dyn_round = t;

    env->fpcr_flush_to_zero
      = (val & (FPCR_UNDZ|FPCR_UNFD)) == (FPCR_UNDZ|FPCR_UNFD);

    env->fpcr_dnz = (val & FPCR_DNZ) != 0;
    env->fpcr_dnod = (val & FPCR_DNOD) != 0;
    env->fpcr_undz = (val & FPCR_UNDZ) != 0;
}

#if defined(CONFIG_USER_ONLY)

int cpu_alpha_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                                int mmu_idx, int is_softmmu)
{
    if (rw == 2)
        env->exception_index = EXCP_ITB_MISS;
    else
        env->exception_index = EXCP_DFAULT;
    env->trap_arg0 = address;
    return 1;
}

void do_interrupt (CPUState *env)
{
    env->exception_index = -1;
}

#else

target_phys_addr_t cpu_get_phys_page_debug (CPUState *env, target_ulong addr)
{
    return -1;
}

int cpu_alpha_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                                int mmu_idx, int is_softmmu)
{
    return 0;
}

void do_interrupt (CPUState *env)
{
    abort();
}
#endif

void cpu_dump_state (CPUState *env, FILE *f, fprintf_function cpu_fprintf,
                     int flags)
{
    static const char *linux_reg_names[] = {
        "v0 ", "t0 ", "t1 ", "t2 ", "t3 ", "t4 ", "t5 ", "t6 ",
        "t7 ", "s0 ", "s1 ", "s2 ", "s3 ", "s4 ", "s5 ", "fp ",
        "a0 ", "a1 ", "a2 ", "a3 ", "a4 ", "a5 ", "t8 ", "t9 ",
        "t10", "t11", "ra ", "t12", "at ", "gp ", "sp ", "zero",
    };
    int i;

    cpu_fprintf(f, "     PC  " TARGET_FMT_lx "      PS  %02x\n",
                env->pc, env->ps);
    for (i = 0; i < 31; i++) {
        cpu_fprintf(f, "IR%02d %s " TARGET_FMT_lx " ", i,
                    linux_reg_names[i], env->ir[i]);
        if ((i % 3) == 2)
            cpu_fprintf(f, "\n");
    }

    cpu_fprintf(f, "lock_a   " TARGET_FMT_lx " lock_v   " TARGET_FMT_lx "\n",
                env->lock_addr, env->lock_value);

    for (i = 0; i < 31; i++) {
        cpu_fprintf(f, "FIR%02d    " TARGET_FMT_lx " ", i,
                    *((uint64_t *)(&env->fir[i])));
        if ((i % 3) == 2)
            cpu_fprintf(f, "\n");
    }
    cpu_fprintf(f, "\n");
}
