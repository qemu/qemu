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
uint32_t do_load_fpscr (void)
{
    return (fpscr_fx  << FPSCR_FX) |
        (fpscr_fex    << FPSCR_FEX) |
        (fpscr_vx     << FPSCR_VX) |
        (fpscr_ox     << FPSCR_OX) |
        (fpscr_ux     << FPSCR_UX) |
        (fpscr_zx     << FPSCR_ZX) |
        (fpscr_xx     << FPSCR_XX) |
        (fpscr_vsxnan << FPSCR_VXSNAN) |
        (fpscr_vxisi  << FPSCR_VXISI) |
        (fpscr_vxidi  << FPSCR_VXIDI) |
        (fpscr_vxzdz  << FPSCR_VXZDZ) |
        (fpscr_vximz  << FPSCR_VXIMZ) |
        (fpscr_fr     << FPSCR_FR) |
        (fpscr_fi     << FPSCR_FI) |
        (fpscr_fprf   << FPSCR_FPRF) |
        (fpscr_vxsoft << FPSCR_VXSOFT) |
        (fpscr_vxsqrt << FPSCR_VXSQRT) |
        (fpscr_oe     << FPSCR_OE) |
        (fpscr_ue     << FPSCR_UE) |
        (fpscr_ze     << FPSCR_ZE) |
        (fpscr_xe     << FPSCR_XE) |
        (fpscr_ni     << FPSCR_NI) |
        (fpscr_rn     << FPSCR_RN);
}

/* We keep only 32 bits of input... */
/* For now, this is COMPLETELY BUGGY ! */
void do_store_fpscr (uint8_t mask, uint32_t fp)
{
    int i;

    for (i = 0; i < 7; i++) {
        if ((mask & (1 << i)) == 0)
            fp &= ~(0xf << (4 * i));
    }
    if ((mask & 80) != 0)
        fpscr_fx = (fp >> FPSCR_FX) & 0x01;
    fpscr_fex = (fp >> FPSCR_FEX) & 0x01;
    fpscr_vx = (fp >> FPSCR_VX) & 0x01;
    fpscr_ox = (fp >> FPSCR_OX) & 0x01;
    fpscr_ux = (fp >> FPSCR_UX) & 0x01;
    fpscr_zx = (fp >> FPSCR_ZX) & 0x01;
    fpscr_xx = (fp >> FPSCR_XX) & 0x01;
    fpscr_vsxnan = (fp >> FPSCR_VXSNAN) & 0x01;
    fpscr_vxisi = (fp >> FPSCR_VXISI) & 0x01;
    fpscr_vxidi = (fp >> FPSCR_VXIDI) & 0x01;
    fpscr_vxzdz = (fp >> FPSCR_VXZDZ) & 0x01;
    fpscr_vximz = (fp >> FPSCR_VXIMZ) & 0x01;
    fpscr_fr = (fp >> FPSCR_FR) & 0x01;
    fpscr_fi = (fp >> FPSCR_FI) & 0x01;
    fpscr_fprf = (fp >> FPSCR_FPRF) & 0x1F;
    fpscr_vxsoft = (fp >> FPSCR_VXSOFT) & 0x01;
    fpscr_vxsqrt = (fp >> FPSCR_VXSQRT) & 0x01;
    fpscr_oe = (fp >> FPSCR_OE) & 0x01;
    fpscr_ue = (fp >> FPSCR_UE) & 0x01;
    fpscr_ze = (fp >> FPSCR_ZE) & 0x01;
    fpscr_xe = (fp >> FPSCR_XE) & 0x01;
    fpscr_ni = (fp >> FPSCR_NI) & 0x01;
    fpscr_rn = (fp >> FPSCR_RN) & 0x03;
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
        if (reg == 32)
            reg = 0;
        ugpr(reg++) = ld32(src);
    }
    if (count > 0) {
        for (sh = 24, tmp = 0; count > 0; count--, src++, sh -= 8) {
            if (reg == 32)
                reg = 0;
            tmp |= ld8(src) << sh;
            if (sh == 0) {
                sh = 32;
                ugpr(reg++) = tmp;
                tmp = 0;
            }
        }
        ugpr(reg) = tmp;
    }
}

void do_stsw (uint32_t reg, int count, uint32_t dest)
{
    int sh;

    for (; count > 3; count -= 4, dest += 4) {
        if (reg == 32)
            reg = 0;
        st32(dest, ugpr(reg++));
    }
    if (count > 0) {
        for (sh = 24; count > 0; count--, dest++, sh -= 8) {
            if (reg == 32)
                reg = 0;
            st8(dest, (ugpr(reg) >> sh) & 0xFF);
            if (sh == 0) {
                sh = 32;
                reg++;
            }
        }
    }
}
