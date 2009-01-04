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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "cpu.h"
#include "exec-all.h"

#if defined(CONFIG_USER_ONLY)

int cpu_alpha_handle_mmu_fault (CPUState *env, target_ulong address, int rw,
                                int mmu_idx, int is_softmmu)
{
    if (rw == 2)
        env->exception_index = EXCP_ITB_MISS;
    else
        env->exception_index = EXCP_DFAULT;
    env->ipr[IPR_EXC_ADDR] = address;

    return 1;
}

target_phys_addr_t cpu_get_phys_page_debug (CPUState *env, target_ulong addr)
{
    return addr;
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
    uint32_t opc;

    if (rw == 2) {
        /* Instruction translation buffer miss */
        env->exception_index = EXCP_ITB_MISS;
    } else {
        if (env->ipr[IPR_EXC_ADDR] & 1)
            env->exception_index = EXCP_DTB_MISS_PAL;
        else
            env->exception_index = EXCP_DTB_MISS_NATIVE;
        opc = (ldl_code(env->pc) >> 21) << 4;
        if (rw) {
            opc |= 0x9;
        } else {
            opc |= 0x4;
        }
        env->ipr[IPR_MM_STAT] = opc;
    }

    return 1;
}

int cpu_alpha_mfpr (CPUState *env, int iprn, uint64_t *valp)
{
    uint64_t hwpcb;
    int ret = 0;

    hwpcb = env->ipr[IPR_PCBB];
    switch (iprn) {
    case IPR_ASN:
        if (env->features & FEATURE_ASN)
            *valp = env->ipr[IPR_ASN];
        else
            *valp = 0;
        break;
    case IPR_ASTEN:
        *valp = ((int64_t)(env->ipr[IPR_ASTEN] << 60)) >> 60;
        break;
    case IPR_ASTSR:
        *valp = ((int64_t)(env->ipr[IPR_ASTSR] << 60)) >> 60;
        break;
    case IPR_DATFX:
        /* Write only */
        ret = -1;
        break;
    case IPR_ESP:
        if (env->features & FEATURE_SPS)
            *valp = env->ipr[IPR_ESP];
        else
            *valp = ldq_raw(hwpcb + 8);
        break;
    case IPR_FEN:
        *valp = ((int64_t)(env->ipr[IPR_FEN] << 63)) >> 63;
        break;
    case IPR_IPIR:
        /* Write-only */
        ret = -1;
        break;
    case IPR_IPL:
        *valp = ((int64_t)(env->ipr[IPR_IPL] << 59)) >> 59;
        break;
    case IPR_KSP:
        if (!(env->ipr[IPR_EXC_ADDR] & 1)) {
            ret = -1;
        } else {
            if (env->features & FEATURE_SPS)
                *valp = env->ipr[IPR_KSP];
            else
                *valp = ldq_raw(hwpcb + 0);
        }
        break;
    case IPR_MCES:
        *valp = ((int64_t)(env->ipr[IPR_MCES] << 59)) >> 59;
        break;
    case IPR_PERFMON:
        /* Implementation specific */
        *valp = 0;
        break;
    case IPR_PCBB:
        *valp = ((int64_t)env->ipr[IPR_PCBB] << 16) >> 16;
        break;
    case IPR_PRBR:
        *valp = env->ipr[IPR_PRBR];
        break;
    case IPR_PTBR:
        *valp = env->ipr[IPR_PTBR];
        break;
    case IPR_SCBB:
        *valp = (int64_t)((int32_t)env->ipr[IPR_SCBB]);
        break;
    case IPR_SIRR:
        /* Write-only */
        ret = -1;
        break;
    case IPR_SISR:
        *valp = (int64_t)((int16_t)env->ipr[IPR_SISR]);
    case IPR_SSP:
        if (env->features & FEATURE_SPS)
            *valp = env->ipr[IPR_SSP];
        else
            *valp = ldq_raw(hwpcb + 16);
        break;
    case IPR_SYSPTBR:
        if (env->features & FEATURE_VIRBND)
            *valp = env->ipr[IPR_SYSPTBR];
        else
            ret = -1;
        break;
    case IPR_TBCHK:
        if ((env->features & FEATURE_TBCHK)) {
            /* XXX: TODO */
            *valp = 0;
            ret = -1;
        } else {
            ret = -1;
        }
        break;
    case IPR_TBIA:
        /* Write-only */
        ret = -1;
        break;
    case IPR_TBIAP:
        /* Write-only */
        ret = -1;
        break;
    case IPR_TBIS:
        /* Write-only */
        ret = -1;
        break;
    case IPR_TBISD:
        /* Write-only */
        ret = -1;
        break;
    case IPR_TBISI:
        /* Write-only */
        ret = -1;
        break;
    case IPR_USP:
        if (env->features & FEATURE_SPS)
            *valp = env->ipr[IPR_USP];
        else
            *valp = ldq_raw(hwpcb + 24);
        break;
    case IPR_VIRBND:
        if (env->features & FEATURE_VIRBND)
            *valp = env->ipr[IPR_VIRBND];
        else
            ret = -1;
        break;
    case IPR_VPTB:
        *valp = env->ipr[IPR_VPTB];
        break;
    case IPR_WHAMI:
        *valp = env->ipr[IPR_WHAMI];
        break;
    default:
        /* Invalid */
        ret = -1;
        break;
    }

    return ret;
}

int cpu_alpha_mtpr (CPUState *env, int iprn, uint64_t val, uint64_t *oldvalp)
{
    uint64_t hwpcb, tmp64;
    uint8_t tmp8;
    int ret = 0;

    hwpcb = env->ipr[IPR_PCBB];
    switch (iprn) {
    case IPR_ASN:
        /* Read-only */
        ret = -1;
        break;
    case IPR_ASTEN:
        tmp8 = ((int8_t)(env->ipr[IPR_ASTEN] << 4)) >> 4;
        *oldvalp = tmp8;
        tmp8 &= val & 0xF;
        tmp8 |= (val >> 4) & 0xF;
        env->ipr[IPR_ASTEN] &= ~0xF;
        env->ipr[IPR_ASTEN] |= tmp8;
        ret = 1;
        break;
    case IPR_ASTSR:
        tmp8 = ((int8_t)(env->ipr[IPR_ASTSR] << 4)) >> 4;
        *oldvalp = tmp8;
        tmp8 &= val & 0xF;
        tmp8 |= (val >> 4) & 0xF;
        env->ipr[IPR_ASTSR] &= ~0xF;
        env->ipr[IPR_ASTSR] |= tmp8;
        ret = 1;
    case IPR_DATFX:
        env->ipr[IPR_DATFX] &= ~0x1;
        env->ipr[IPR_DATFX] |= val & 1;
        tmp64 = ldq_raw(hwpcb + 56);
        tmp64 &= ~0x8000000000000000ULL;
        tmp64 |= (val & 1) << 63;
        stq_raw(hwpcb + 56, tmp64);
        break;
    case IPR_ESP:
        if (env->features & FEATURE_SPS)
            env->ipr[IPR_ESP] = val;
        else
            stq_raw(hwpcb + 8, val);
        break;
    case IPR_FEN:
        env->ipr[IPR_FEN] = val & 1;
        tmp64 = ldq_raw(hwpcb + 56);
        tmp64 &= ~1;
        tmp64 |= val & 1;
        stq_raw(hwpcb + 56, tmp64);
        break;
    case IPR_IPIR:
        /* XXX: TODO: Send IRQ to CPU #ir[16] */
        break;
    case IPR_IPL:
        *oldvalp = ((int64_t)(env->ipr[IPR_IPL] << 59)) >> 59;
        env->ipr[IPR_IPL] &= ~0x1F;
        env->ipr[IPR_IPL] |= val & 0x1F;
        /* XXX: may issue an interrupt or ASR _now_ */
        ret = 1;
        break;
    case IPR_KSP:
        if (!(env->ipr[IPR_EXC_ADDR] & 1)) {
            ret = -1;
        } else {
            if (env->features & FEATURE_SPS)
                env->ipr[IPR_KSP] = val;
            else
                stq_raw(hwpcb + 0, val);
        }
        break;
    case IPR_MCES:
        env->ipr[IPR_MCES] &= ~((val & 0x7) | 0x18);
        env->ipr[IPR_MCES] |= val & 0x18;
        break;
    case IPR_PERFMON:
        /* Implementation specific */
        *oldvalp = 0;
        ret = 1;
        break;
    case IPR_PCBB:
        /* Read-only */
        ret = -1;
        break;
    case IPR_PRBR:
        env->ipr[IPR_PRBR] = val;
        break;
    case IPR_PTBR:
        /* Read-only */
        ret = -1;
        break;
    case IPR_SCBB:
        env->ipr[IPR_SCBB] = (uint32_t)val;
        break;
    case IPR_SIRR:
        if (val & 0xF) {
            env->ipr[IPR_SISR] |= 1 << (val & 0xF);
            /* XXX: request a software interrupt _now_ */
        }
        break;
    case IPR_SISR:
        /* Read-only */
        ret = -1;
        break;
    case IPR_SSP:
        if (env->features & FEATURE_SPS)
            env->ipr[IPR_SSP] = val;
        else
            stq_raw(hwpcb + 16, val);
        break;
    case IPR_SYSPTBR:
        if (env->features & FEATURE_VIRBND)
            env->ipr[IPR_SYSPTBR] = val;
        else
            ret = -1;
    case IPR_TBCHK:
        /* Read-only */
        ret = -1;
        break;
    case IPR_TBIA:
        tlb_flush(env, 1);
        break;
    case IPR_TBIAP:
        tlb_flush(env, 1);
        break;
    case IPR_TBIS:
        tlb_flush_page(env, val);
        break;
    case IPR_TBISD:
        tlb_flush_page(env, val);
        break;
    case IPR_TBISI:
        tlb_flush_page(env, val);
        break;
    case IPR_USP:
        if (env->features & FEATURE_SPS)
            env->ipr[IPR_USP] = val;
        else
            stq_raw(hwpcb + 24, val);
        break;
    case IPR_VIRBND:
        if (env->features & FEATURE_VIRBND)
            env->ipr[IPR_VIRBND] = val;
        else
            ret = -1;
        break;
    case IPR_VPTB:
        env->ipr[IPR_VPTB] = val;
        break;
    case IPR_WHAMI:
        /* Read-only */
        ret = -1;
        break;
    default:
        /* Invalid */
        ret = -1;
        break;
    }

    return ret;
}

void do_interrupt (CPUState *env)
{
    int excp;

    env->ipr[IPR_EXC_ADDR] = env->pc | 1;
    excp = env->exception_index;
    env->exception_index = 0;
    env->error_code = 0;
    /* XXX: disable interrupts and memory mapping */
    if (env->ipr[IPR_PAL_BASE] != -1ULL) {
        /* We use native PALcode */
        env->pc = env->ipr[IPR_PAL_BASE] + excp;
    } else {
        /* We use emulated PALcode */
        call_pal(env);
        /* Emulate REI */
        env->pc = env->ipr[IPR_EXC_ADDR] & ~7;
        env->ipr[IPR_EXC_ADDR] = env->ipr[IPR_EXC_ADDR] & 1;
        /* XXX: re-enable interrupts and memory mapping */
    }
}
#endif

void cpu_dump_state (CPUState *env, FILE *f,
                     int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                     int flags)
{
    static const char *linux_reg_names[] = {
        "v0 ", "t0 ", "t1 ", "t2 ", "t3 ", "t4 ", "t5 ", "t6 ",
        "t7 ", "s0 ", "s1 ", "s2 ", "s3 ", "s4 ", "s5 ", "fp ",
        "a0 ", "a1 ", "a2 ", "a3 ", "a4 ", "a5 ", "t8 ", "t9 ",
        "t10", "t11", "ra ", "t12", "at ", "gp ", "sp ", "zero",
    };
    int i;

    cpu_fprintf(f, "     PC  " TARGET_FMT_lx "      PS  " TARGET_FMT_lx "\n",
                env->pc, env->ps);
    for (i = 0; i < 31; i++) {
        cpu_fprintf(f, "IR%02d %s " TARGET_FMT_lx " ", i,
                    linux_reg_names[i], env->ir[i]);
        if ((i % 3) == 2)
            cpu_fprintf(f, "\n");
    }
    cpu_fprintf(f, "\n");
    for (i = 0; i < 31; i++) {
        cpu_fprintf(f, "FIR%02d    " TARGET_FMT_lx " ", i,
                    *((uint64_t *)(&env->fir[i])));
        if ((i % 3) == 2)
            cpu_fprintf(f, "\n");
    }
    cpu_fprintf(f, "\nlock     " TARGET_FMT_lx "\n", env->lock);
}
