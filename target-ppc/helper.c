/*
 *  PPC emulation helpers for qemu.
 * 
 *  Copyright (c) 2003 Jocelyn Mayer
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "exec.h"

extern FILE *logfile;

void cpu_loop_exit(void)
{
    longjmp(env->jmp_env, 1);
}

/* shortcuts to generate exceptions */
void raise_exception_err (int exception_index, int error_code)
{
    env->exception_index = exception_index;
    env->error_code = error_code;

    cpu_loop_exit();
}

void raise_exception (int exception_index)
{
    env->exception_index = exception_index;
    env->error_code = 0;

    cpu_loop_exit();
}

/* Helpers for "fat" micro operations */
uint32_t do_load_cr (void)
{
    return (env->crf[0] << 28) |
        (env->crf[1] << 24) |
        (env->crf[2] << 20) |
        (env->crf[3] << 16) |
        (env->crf[4] << 12) |
        (env->crf[5] << 8) |
        (env->crf[6] << 4) |
        (env->crf[7] << 0);
}

void do_store_cr (uint32_t crn, uint32_t value)
{
    int i, sh;

    for (i = 0, sh = 7; i < 8; i++, sh --) {
        if (crn & (1 << sh))
            env->crf[i] = (value >> (sh * 4)) & 0xF;
    }
}

uint32_t do_load_xer (void)
{
    return (xer_so << XER_SO) |
        (xer_ov << XER_OV) |
        (xer_ca << XER_CA) |
        (xer_bc << XER_BC);
}

void do_store_xer (uint32_t value)
{
    xer_so = (value >> XER_SO) & 0x01;
    xer_ov = (value >> XER_OV) & 0x01;
    xer_ca = (value >> XER_CA) & 0x01;
    xer_bc = (value >> XER_BC) & 0x1f;
}

uint32_t do_load_msr (void)
{
    return (msr_pow << MSR_POW) |
        (msr_ile << MSR_ILE) |
        (msr_ee << MSR_EE) |
        (msr_pr << MSR_PR) |
        (msr_fp << MSR_FP) |
        (msr_me << MSR_ME) |
        (msr_fe0 << MSR_FE0) |
        (msr_se << MSR_SE) |
        (msr_be << MSR_BE) |
        (msr_fe1 << MSR_FE1) |
        (msr_ip << MSR_IP) |
        (msr_ir << MSR_IR) |
        (msr_dr << MSR_DR) |
        (msr_ri << MSR_RI) |
        (msr_le << MSR_LE);
}

void do_store_msr (uint32_t msr_value)
{
    msr_pow = (msr_value >> MSR_POW) & 0x03;
    msr_ile = (msr_value >> MSR_ILE) & 0x01;
    msr_ee = (msr_value >> MSR_EE) & 0x01;
    msr_pr = (msr_value >> MSR_PR) & 0x01;
    msr_fp = (msr_value >> MSR_FP) & 0x01;
    msr_me = (msr_value >> MSR_ME) & 0x01;
    msr_fe0 = (msr_value >> MSR_FE0) & 0x01;
    msr_se = (msr_value >> MSR_SE) & 0x01;
    msr_be = (msr_value >> MSR_BE) & 0x01;
    msr_fe1 = (msr_value >> MSR_FE1) & 0x01;
    msr_ip = (msr_value >> MSR_IP) & 0x01;
    msr_ir = (msr_value >> MSR_IR) & 0x01;
    msr_dr = (msr_value >> MSR_DR) & 0x01;
    msr_ri = (msr_value >> MSR_RI) & 0x01;
    msr_le = (msr_value >> MSR_LE) & 0x01;
}

/* The 32 MSB of the target fpr are undefined. They'll be zero... */
/* Floating point operations helpers */
void do_load_fpscr (void)
{
    /* The 32 MSB of the target fpr are undefined.
     * They'll be zero...
     */
    union {
        double d;
        struct {
            uint32_t u[2];
        } s;
    } u;
    int i;

    u.s.u[0] = 0;
    u.s.u[1] = 0;
    for (i = 0; i < 8; i++)
        u.s.u[1] |= env->fpscr[i] << (4 * i);
    FT0 = u.d;
}

void do_store_fpscr (uint32_t mask)
{
    /*
     * We use only the 32 LSB of the incoming fpr
     */
    union {
        double d;
        struct {
            uint32_t u[2];
        } s;
    } u;
    int i;

    u.d = FT0;
    if (mask & 0x80)
        env->fpscr[0] = (env->fpscr[0] & 0x9) | ((u.s.u[1] >> 28) & ~0x9);
    for (i = 1; i < 7; i++) {
        if (mask & (1 << (7 - i)))
            env->fpscr[i] = (u.s.u[1] >> (4 * (7 - i))) & 0xF;
    }
    /* TODO: update FEX & VX */
    /* Set rounding mode */
    switch (env->fpscr[0] & 0x3) {
    case 0:
        /* Best approximation (round to nearest) */
        fesetround(FE_TONEAREST);
        break;
    case 1:
        /* Smaller magnitude (round toward zero) */
        fesetround(FE_TOWARDZERO);
        break;
    case 2:
        /* Round toward +infinite */
        fesetround(FE_UPWARD);
        break;
    case 3:
        /* Round toward -infinite */
        fesetround(FE_DOWNWARD);
        break;
    }
}

int32_t do_sraw(int32_t value, uint32_t shift)
{
    int32_t ret;

    xer_ca = 0;
    if (shift & 0x20) {
        ret = (-1) * ((uint32_t)value >> 31);
        if (ret < 0)
            xer_ca = 1;
    } else {
        ret = value >> (shift & 0x1f);
        if (ret < 0 && (value & ((1 << shift) - 1)) != 0)
            xer_ca = 1;
    }

    return ret;
}

void do_lmw (int reg, uint32_t src)
{
    for (; reg <= 31; reg++, src += 4)
        ugpr(reg) = ld32(src);
}

void do_stmw (int reg, uint32_t dest)
{
    for (; reg <= 31; reg++, dest += 4)
        st32(dest, ugpr(reg));
}

void do_lsw (uint32_t reg, int count, uint32_t src)
{
    uint32_t tmp;
    int sh;
    
    for (; count > 3; count -= 4, src += 4) {
        ugpr(reg++) = ld32(src);
        if (T2 == 32)
            T2 = 0;
    }
    if (count > 0) {
                tmp = 0;
        for (sh = 24; count > 0; count--, src++, sh -= 8) {
            tmp |= ld8(src) << sh;
        }
        ugpr(reg) = tmp;
    }
}

void do_stsw (uint32_t reg, int count, uint32_t dest)
{
    int sh;

    for (; count > 3; count -= 4, dest += 4) {
        st32(dest, ugpr(reg++));
        if (reg == 32)
            reg = 0;
    }
    if (count > 0) {
        for (sh = 24; count > 0; count--, dest++, sh -= 8) {
            st8(dest, (ugpr(reg) >> sh) & 0xFF);
            }
        }
}

void do_dcbz (void)
{
    int i;

    /* Assume cache line size is 32 */
    for (i = 0; i < 8; i++) {
        st32(T0, 0);
        T0 += 4;
    }
}
    
/* Instruction cache invalidation helper */
void do_icbi (void)
{
    tb_invalidate_page(T0);
}
