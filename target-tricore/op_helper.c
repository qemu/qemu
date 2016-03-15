/*
 *  Copyright (c) 2012-2014 Bastian Koppelmann C-Lab/University Paderborn
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
#include "qemu/osdep.h"
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include <zlib.h> /* for crc32 */


/* Exception helpers */

static void QEMU_NORETURN
raise_exception_sync_internal(CPUTriCoreState *env, uint32_t class, int tin,
                              uintptr_t pc, uint32_t fcd_pc)
{
    CPUState *cs = CPU(tricore_env_get_cpu(env));
    /* in case we come from a helper-call we need to restore the PC */
    if (pc) {
        cpu_restore_state(cs, pc);
    }

    /* Tin is loaded into d[15] */
    env->gpr_d[15] = tin;

    if (class == TRAPC_CTX_MNG && tin == TIN3_FCU) {
        /* upper context cannot be saved, if the context list is empty */
    } else {
        helper_svucx(env);
    }

    /* The return address in a[11] is updated */
    if (class == TRAPC_CTX_MNG && tin == TIN3_FCD) {
        env->SYSCON |= MASK_SYSCON_FCD_SF;
        /* when we run out of CSAs after saving a context a FCD trap is taken
           and the return address is the start of the trap handler which used
           the last CSA */
        env->gpr_a[11] = fcd_pc;
    } else if (class == TRAPC_SYSCALL) {
        env->gpr_a[11] = env->PC + 4;
    } else {
        env->gpr_a[11] = env->PC;
    }
    /* The stack pointer in A[10] is set to the Interrupt Stack Pointer (ISP)
       when the processor was not previously using the interrupt stack
       (in case of PSW.IS = 0). The stack pointer bit is set for using the
       interrupt stack: PSW.IS = 1. */
    if ((env->PSW & MASK_PSW_IS) == 0) {
        env->gpr_a[10] = env->ISP;
    }
    env->PSW |= MASK_PSW_IS;
    /* The I/O mode is set to Supervisor mode, which means all permissions
       are enabled: PSW.IO = 10 B .*/
    env->PSW |= (2 << 10);

    /*The current Protection Register Set is set to 0: PSW.PRS = 00 B .*/
    env->PSW &= ~MASK_PSW_PRS;

    /* The Call Depth Counter (CDC) is cleared, and the call depth limit is
       set for 64: PSW.CDC = 0000000 B .*/
    env->PSW &= ~MASK_PSW_CDC;

    /* Call Depth Counter is enabled, PSW.CDE = 1. */
    env->PSW |= MASK_PSW_CDE;

    /* Write permission to global registers A[0], A[1], A[8], A[9] is
       disabled: PSW.GW = 0. */
    env->PSW &= ~MASK_PSW_GW;

    /*The interrupt system is globally disabled: ICR.IE = 0. The ‘old’
      ICR.IE and ICR.CCPN are saved */

    /* PCXI.PIE = ICR.IE */
    env->PCXI = ((env->PCXI & ~MASK_PCXI_PIE) +
                ((env->ICR & MASK_ICR_IE) << 15));
    /* PCXI.PCPN = ICR.CCPN */
    env->PCXI = (env->PCXI & 0xffffff) +
                ((env->ICR & MASK_ICR_CCPN) << 24);
    /* Update PC using the trap vector table */
    env->PC = env->BTV | (class << 5);

    cpu_loop_exit(cs);
}

void helper_raise_exception_sync(CPUTriCoreState *env, uint32_t class,
                                 uint32_t tin)
{
    raise_exception_sync_internal(env, class, tin, 0, 0);
}

static void raise_exception_sync_helper(CPUTriCoreState *env, uint32_t class,
                                        uint32_t tin, uintptr_t pc)
{
    raise_exception_sync_internal(env, class, tin, pc, 0);
}

/* Addressing mode helper */

static uint16_t reverse16(uint16_t val)
{
    uint8_t high = (uint8_t)(val >> 8);
    uint8_t low  = (uint8_t)(val & 0xff);

    uint16_t rh, rl;

    rl = (uint16_t)((high * 0x0202020202ULL & 0x010884422010ULL) % 1023);
    rh = (uint16_t)((low * 0x0202020202ULL & 0x010884422010ULL) % 1023);

    return (rh << 8) | rl;
}

uint32_t helper_br_update(uint32_t reg)
{
    uint32_t index = reg & 0xffff;
    uint32_t incr  = reg >> 16;
    uint32_t new_index = reverse16(reverse16(index) + reverse16(incr));
    return reg - index + new_index;
}

uint32_t helper_circ_update(uint32_t reg, uint32_t off)
{
    uint32_t index = reg & 0xffff;
    uint32_t length = reg >> 16;
    int32_t new_index = index + off;
    if (new_index < 0) {
        new_index += length;
    } else {
        new_index %= length;
    }
    return reg - index + new_index;
}

static uint32_t ssov32(CPUTriCoreState *env, int64_t arg)
{
    uint32_t ret;
    int64_t max_pos = INT32_MAX;
    int64_t max_neg = INT32_MIN;
    if (arg > max_pos) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV = (1 << 31);
        ret = (target_ulong)max_pos;
    } else {
        if (arg < max_neg) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            ret = (target_ulong)max_neg;
        } else {
            env->PSW_USB_V = 0;
            ret = (target_ulong)arg;
        }
    }
    env->PSW_USB_AV = arg ^ arg * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;
    return ret;
}

static uint32_t suov32_pos(CPUTriCoreState *env, uint64_t arg)
{
    uint32_t ret;
    uint64_t max_pos = UINT32_MAX;
    if (arg > max_pos) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV = (1 << 31);
        ret = (target_ulong)max_pos;
    } else {
        env->PSW_USB_V = 0;
        ret = (target_ulong)arg;
     }
    env->PSW_USB_AV = arg ^ arg * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;
    return ret;
}

static uint32_t suov32_neg(CPUTriCoreState *env, int64_t arg)
{
    uint32_t ret;

    if (arg < 0) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV = (1 << 31);
        ret = 0;
    } else {
        env->PSW_USB_V = 0;
        ret = (target_ulong)arg;
    }
    env->PSW_USB_AV = arg ^ arg * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;
    return ret;
}

static uint32_t ssov16(CPUTriCoreState *env, int32_t hw0, int32_t hw1)
{
    int32_t max_pos = INT16_MAX;
    int32_t max_neg = INT16_MIN;
    int32_t av0, av1;

    env->PSW_USB_V = 0;
    av0 = hw0 ^ hw0 * 2u;
    if (hw0 > max_pos) {
        env->PSW_USB_V = (1 << 31);
        hw0 = max_pos;
    } else if (hw0 < max_neg) {
        env->PSW_USB_V = (1 << 31);
        hw0 = max_neg;
    }

    av1 = hw1 ^ hw1 * 2u;
    if (hw1 > max_pos) {
        env->PSW_USB_V = (1 << 31);
        hw1 = max_pos;
    } else if (hw1 < max_neg) {
        env->PSW_USB_V = (1 << 31);
        hw1 = max_neg;
    }

    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = (av0 | av1) << 16;
    env->PSW_USB_SAV |= env->PSW_USB_AV;
    return (hw0 & 0xffff) | (hw1 << 16);
}

static uint32_t suov16(CPUTriCoreState *env, int32_t hw0, int32_t hw1)
{
    int32_t max_pos = UINT16_MAX;
    int32_t av0, av1;

    env->PSW_USB_V = 0;
    av0 = hw0 ^ hw0 * 2u;
    if (hw0 > max_pos) {
        env->PSW_USB_V = (1 << 31);
        hw0 = max_pos;
    } else if (hw0 < 0) {
        env->PSW_USB_V = (1 << 31);
        hw0 = 0;
    }

    av1 = hw1 ^ hw1 * 2u;
    if (hw1 > max_pos) {
        env->PSW_USB_V = (1 << 31);
        hw1 = max_pos;
    } else if (hw1 < 0) {
        env->PSW_USB_V = (1 << 31);
        hw1 = 0;
    }

    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = (av0 | av1) << 16;
    env->PSW_USB_SAV |= env->PSW_USB_AV;
    return (hw0 & 0xffff) | (hw1 << 16);
}

target_ulong helper_add_ssov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t result = t1 + t2;
    return ssov32(env, result);
}

uint64_t helper_add64_ssov(CPUTriCoreState *env, uint64_t r1, uint64_t r2)
{
    uint64_t result;
    int64_t ovf;

    result = r1 + r2;
    ovf = (result ^ r1) & ~(r1 ^ r2);
    env->PSW_USB_AV = (result ^ result * 2u) >> 32;
    env->PSW_USB_SAV |= env->PSW_USB_AV;
    if (ovf < 0) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV = (1 << 31);
        /* ext_ret > MAX_INT */
        if ((int64_t)r1 >= 0) {
            result = INT64_MAX;
        /* ext_ret < MIN_INT */
        } else {
            result = INT64_MIN;
        }
    } else {
        env->PSW_USB_V = 0;
    }
    return result;
}

target_ulong helper_add_h_ssov(CPUTriCoreState *env, target_ulong r1,
                               target_ulong r2)
{
    int32_t ret_hw0, ret_hw1;

    ret_hw0 = sextract32(r1, 0, 16) + sextract32(r2, 0, 16);
    ret_hw1 = sextract32(r1, 16, 16) + sextract32(r2, 16, 16);
    return ssov16(env, ret_hw0, ret_hw1);
}

uint32_t helper_addr_h_ssov(CPUTriCoreState *env, uint64_t r1, uint32_t r2_l,
                            uint32_t r2_h)
{
    int64_t mul_res0 = sextract64(r1, 0, 32);
    int64_t mul_res1 = sextract64(r1, 32, 32);
    int64_t r2_low = sextract64(r2_l, 0, 32);
    int64_t r2_high = sextract64(r2_h, 0, 32);
    int64_t result0, result1;
    uint32_t ovf0, ovf1;
    uint32_t avf0, avf1;

    ovf0 = ovf1 = 0;

    result0 = r2_low + mul_res0 + 0x8000;
    result1 = r2_high + mul_res1 + 0x8000;

    avf0 = result0 * 2u;
    avf0 = result0 ^ avf0;
    avf1 = result1 * 2u;
    avf1 = result1 ^ avf1;

    if (result0 > INT32_MAX) {
        ovf0 = (1 << 31);
        result0 = INT32_MAX;
    } else if (result0 < INT32_MIN) {
        ovf0 = (1 << 31);
        result0 = INT32_MIN;
    }

    if (result1 > INT32_MAX) {
        ovf1 = (1 << 31);
        result1 = INT32_MAX;
    } else if (result1 < INT32_MIN) {
        ovf1 = (1 << 31);
        result1 = INT32_MIN;
    }

    env->PSW_USB_V = ovf0 | ovf1;
    env->PSW_USB_SV |= env->PSW_USB_V;

    env->PSW_USB_AV = avf0 | avf1;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return (result1 & 0xffff0000ULL) | ((result0 >> 16) & 0xffffULL);
}

uint32_t helper_addsur_h_ssov(CPUTriCoreState *env, uint64_t r1, uint32_t r2_l,
                              uint32_t r2_h)
{
    int64_t mul_res0 = sextract64(r1, 0, 32);
    int64_t mul_res1 = sextract64(r1, 32, 32);
    int64_t r2_low = sextract64(r2_l, 0, 32);
    int64_t r2_high = sextract64(r2_h, 0, 32);
    int64_t result0, result1;
    uint32_t ovf0, ovf1;
    uint32_t avf0, avf1;

    ovf0 = ovf1 = 0;

    result0 = r2_low - mul_res0 + 0x8000;
    result1 = r2_high + mul_res1 + 0x8000;

    avf0 = result0 * 2u;
    avf0 = result0 ^ avf0;
    avf1 = result1 * 2u;
    avf1 = result1 ^ avf1;

    if (result0 > INT32_MAX) {
        ovf0 = (1 << 31);
        result0 = INT32_MAX;
    } else if (result0 < INT32_MIN) {
        ovf0 = (1 << 31);
        result0 = INT32_MIN;
    }

    if (result1 > INT32_MAX) {
        ovf1 = (1 << 31);
        result1 = INT32_MAX;
    } else if (result1 < INT32_MIN) {
        ovf1 = (1 << 31);
        result1 = INT32_MIN;
    }

    env->PSW_USB_V = ovf0 | ovf1;
    env->PSW_USB_SV |= env->PSW_USB_V;

    env->PSW_USB_AV = avf0 | avf1;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return (result1 & 0xffff0000ULL) | ((result0 >> 16) & 0xffffULL);
}


target_ulong helper_add_suov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    int64_t t1 = extract64(r1, 0, 32);
    int64_t t2 = extract64(r2, 0, 32);
    int64_t result = t1 + t2;
    return suov32_pos(env, result);
}

target_ulong helper_add_h_suov(CPUTriCoreState *env, target_ulong r1,
                               target_ulong r2)
{
    int32_t ret_hw0, ret_hw1;

    ret_hw0 = extract32(r1, 0, 16) + extract32(r2, 0, 16);
    ret_hw1 = extract32(r1, 16, 16) + extract32(r2, 16, 16);
    return suov16(env, ret_hw0, ret_hw1);
}

target_ulong helper_sub_ssov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t result = t1 - t2;
    return ssov32(env, result);
}

uint64_t helper_sub64_ssov(CPUTriCoreState *env, uint64_t r1, uint64_t r2)
{
    uint64_t result;
    int64_t ovf;

    result = r1 - r2;
    ovf = (result ^ r1) & (r1 ^ r2);
    env->PSW_USB_AV = (result ^ result * 2u) >> 32;
    env->PSW_USB_SAV |= env->PSW_USB_AV;
    if (ovf < 0) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV = (1 << 31);
        /* ext_ret > MAX_INT */
        if ((int64_t)r1 >= 0) {
            result = INT64_MAX;
        /* ext_ret < MIN_INT */
        } else {
            result = INT64_MIN;
        }
    } else {
        env->PSW_USB_V = 0;
    }
    return result;
}

target_ulong helper_sub_h_ssov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    int32_t ret_hw0, ret_hw1;

    ret_hw0 = sextract32(r1, 0, 16) - sextract32(r2, 0, 16);
    ret_hw1 = sextract32(r1, 16, 16) - sextract32(r2, 16, 16);
    return ssov16(env, ret_hw0, ret_hw1);
}

uint32_t helper_subr_h_ssov(CPUTriCoreState *env, uint64_t r1, uint32_t r2_l,
                            uint32_t r2_h)
{
    int64_t mul_res0 = sextract64(r1, 0, 32);
    int64_t mul_res1 = sextract64(r1, 32, 32);
    int64_t r2_low = sextract64(r2_l, 0, 32);
    int64_t r2_high = sextract64(r2_h, 0, 32);
    int64_t result0, result1;
    uint32_t ovf0, ovf1;
    uint32_t avf0, avf1;

    ovf0 = ovf1 = 0;

    result0 = r2_low - mul_res0 + 0x8000;
    result1 = r2_high - mul_res1 + 0x8000;

    avf0 = result0 * 2u;
    avf0 = result0 ^ avf0;
    avf1 = result1 * 2u;
    avf1 = result1 ^ avf1;

    if (result0 > INT32_MAX) {
        ovf0 = (1 << 31);
        result0 = INT32_MAX;
    } else if (result0 < INT32_MIN) {
        ovf0 = (1 << 31);
        result0 = INT32_MIN;
    }

    if (result1 > INT32_MAX) {
        ovf1 = (1 << 31);
        result1 = INT32_MAX;
    } else if (result1 < INT32_MIN) {
        ovf1 = (1 << 31);
        result1 = INT32_MIN;
    }

    env->PSW_USB_V = ovf0 | ovf1;
    env->PSW_USB_SV |= env->PSW_USB_V;

    env->PSW_USB_AV = avf0 | avf1;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return (result1 & 0xffff0000ULL) | ((result0 >> 16) & 0xffffULL);
}

uint32_t helper_subadr_h_ssov(CPUTriCoreState *env, uint64_t r1, uint32_t r2_l,
                              uint32_t r2_h)
{
    int64_t mul_res0 = sextract64(r1, 0, 32);
    int64_t mul_res1 = sextract64(r1, 32, 32);
    int64_t r2_low = sextract64(r2_l, 0, 32);
    int64_t r2_high = sextract64(r2_h, 0, 32);
    int64_t result0, result1;
    uint32_t ovf0, ovf1;
    uint32_t avf0, avf1;

    ovf0 = ovf1 = 0;

    result0 = r2_low + mul_res0 + 0x8000;
    result1 = r2_high - mul_res1 + 0x8000;

    avf0 = result0 * 2u;
    avf0 = result0 ^ avf0;
    avf1 = result1 * 2u;
    avf1 = result1 ^ avf1;

    if (result0 > INT32_MAX) {
        ovf0 = (1 << 31);
        result0 = INT32_MAX;
    } else if (result0 < INT32_MIN) {
        ovf0 = (1 << 31);
        result0 = INT32_MIN;
    }

    if (result1 > INT32_MAX) {
        ovf1 = (1 << 31);
        result1 = INT32_MAX;
    } else if (result1 < INT32_MIN) {
        ovf1 = (1 << 31);
        result1 = INT32_MIN;
    }

    env->PSW_USB_V = ovf0 | ovf1;
    env->PSW_USB_SV |= env->PSW_USB_V;

    env->PSW_USB_AV = avf0 | avf1;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return (result1 & 0xffff0000ULL) | ((result0 >> 16) & 0xffffULL);
}

target_ulong helper_sub_suov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    int64_t t1 = extract64(r1, 0, 32);
    int64_t t2 = extract64(r2, 0, 32);
    int64_t result = t1 - t2;
    return suov32_neg(env, result);
}

target_ulong helper_sub_h_suov(CPUTriCoreState *env, target_ulong r1,
                               target_ulong r2)
{
    int32_t ret_hw0, ret_hw1;

    ret_hw0 = extract32(r1, 0, 16) - extract32(r2, 0, 16);
    ret_hw1 = extract32(r1, 16, 16) - extract32(r2, 16, 16);
    return suov16(env, ret_hw0, ret_hw1);
}

target_ulong helper_mul_ssov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t result = t1 * t2;
    return ssov32(env, result);
}

target_ulong helper_mul_suov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    int64_t t1 = extract64(r1, 0, 32);
    int64_t t2 = extract64(r2, 0, 32);
    int64_t result = t1 * t2;

    return suov32_pos(env, result);
}

target_ulong helper_sha_ssov(CPUTriCoreState *env, target_ulong r1,
                             target_ulong r2)
{
    int64_t t1 = sextract64(r1, 0, 32);
    int32_t t2 = sextract64(r2, 0, 6);
    int64_t result;
    if (t2 == 0) {
        result = t1;
    } else if (t2 > 0) {
        result = t1 << t2;
    } else {
        result = t1 >> -t2;
    }
    return ssov32(env, result);
}

uint32_t helper_abs_ssov(CPUTriCoreState *env, target_ulong r1)
{
    target_ulong result;
    result = ((int32_t)r1 >= 0) ? r1 : (0 - r1);
    return ssov32(env, result);
}

uint32_t helper_abs_h_ssov(CPUTriCoreState *env, target_ulong r1)
{
    int32_t ret_h0, ret_h1;

    ret_h0 = sextract32(r1, 0, 16);
    ret_h0 = (ret_h0 >= 0) ? ret_h0 : (0 - ret_h0);

    ret_h1 = sextract32(r1, 16, 16);
    ret_h1 = (ret_h1 >= 0) ? ret_h1 : (0 - ret_h1);

    return ssov16(env, ret_h0, ret_h1);
}

target_ulong helper_absdif_ssov(CPUTriCoreState *env, target_ulong r1,
                                target_ulong r2)
{
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t result;

    if (t1 > t2) {
        result = t1 - t2;
    } else {
        result = t2 - t1;
    }
    return ssov32(env, result);
}

uint32_t helper_absdif_h_ssov(CPUTriCoreState *env, target_ulong r1,
                              target_ulong r2)
{
    int32_t t1, t2;
    int32_t ret_h0, ret_h1;

    t1 = sextract32(r1, 0, 16);
    t2 = sextract32(r2, 0, 16);
    if (t1 > t2) {
        ret_h0 = t1 - t2;
    } else {
        ret_h0 = t2 - t1;
    }

    t1 = sextract32(r1, 16, 16);
    t2 = sextract32(r2, 16, 16);
    if (t1 > t2) {
        ret_h1 = t1 - t2;
    } else {
        ret_h1 = t2 - t1;
    }

    return ssov16(env, ret_h0, ret_h1);
}

target_ulong helper_madd32_ssov(CPUTriCoreState *env, target_ulong r1,
                                target_ulong r2, target_ulong r3)
{
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t t3 = sextract64(r3, 0, 32);
    int64_t result;

    result = t2 + (t1 * t3);
    return ssov32(env, result);
}

target_ulong helper_madd32_suov(CPUTriCoreState *env, target_ulong r1,
                                target_ulong r2, target_ulong r3)
{
    uint64_t t1 = extract64(r1, 0, 32);
    uint64_t t2 = extract64(r2, 0, 32);
    uint64_t t3 = extract64(r3, 0, 32);
    int64_t result;

    result = t2 + (t1 * t3);
    return suov32_pos(env, result);
}

uint64_t helper_madd64_ssov(CPUTriCoreState *env, target_ulong r1,
                            uint64_t r2, target_ulong r3)
{
    uint64_t ret, ovf;
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t3 = sextract64(r3, 0, 32);
    int64_t mul;

    mul = t1 * t3;
    ret = mul + r2;
    ovf = (ret ^ mul) & ~(mul ^ r2);

    t1 = ret >> 32;
    env->PSW_USB_AV = t1 ^ t1 * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    if ((int64_t)ovf < 0) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV = (1 << 31);
        /* ext_ret > MAX_INT */
        if (mul >= 0) {
            ret = INT64_MAX;
        /* ext_ret < MIN_INT */
        } else {
            ret = INT64_MIN;
        }
    } else {
        env->PSW_USB_V = 0;
    }

    return ret;
}

uint32_t
helper_madd32_q_add_ssov(CPUTriCoreState *env, uint64_t r1, uint64_t r2)
{
    int64_t result;

    result = (r1 + r2);

    env->PSW_USB_AV = (result ^ result * 2u);
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    /* we do the saturation by hand, since we produce an overflow on the host
       if the mul before was (0x80000000 * 0x80000000) << 1). If this is the
       case, we flip the saturated value. */
    if (r2 == 0x8000000000000000LL) {
        if (result > 0x7fffffffLL) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            result = INT32_MIN;
        } else if (result < -0x80000000LL) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            result = INT32_MAX;
        } else {
            env->PSW_USB_V = 0;
        }
    } else {
        if (result > 0x7fffffffLL) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            result = INT32_MAX;
        } else if (result < -0x80000000LL) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            result = INT32_MIN;
        } else {
            env->PSW_USB_V = 0;
        }
    }
    return (uint32_t)result;
}

uint64_t helper_madd64_q_ssov(CPUTriCoreState *env, uint64_t r1, uint32_t r2,
                              uint32_t r3, uint32_t n)
{
    int64_t t1 = (int64_t)r1;
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t t3 = sextract64(r3, 0, 32);
    int64_t result, mul;
    int64_t ovf;

    mul = (t2 * t3) << n;
    result = mul + t1;

    env->PSW_USB_AV = (result ^ result * 2u) >> 32;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    ovf = (result ^ mul) & ~(mul ^ t1);
    /* we do the saturation by hand, since we produce an overflow on the host
       if the mul was (0x80000000 * 0x80000000) << 1). If this is the
       case, we flip the saturated value. */
    if ((r2 == 0x80000000) && (r3 == 0x80000000) && (n == 1)) {
        if (ovf >= 0) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            /* ext_ret > MAX_INT */
            if (mul < 0) {
                result = INT64_MAX;
            /* ext_ret < MIN_INT */
            } else {
               result = INT64_MIN;
            }
        } else {
            env->PSW_USB_V = 0;
        }
    } else {
        if (ovf < 0) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            /* ext_ret > MAX_INT */
            if (mul >= 0) {
                result = INT64_MAX;
            /* ext_ret < MIN_INT */
            } else {
               result = INT64_MIN;
            }
        } else {
            env->PSW_USB_V = 0;
        }
    }
    return (uint64_t)result;
}

uint32_t helper_maddr_q_ssov(CPUTriCoreState *env, uint32_t r1, uint32_t r2,
                             uint32_t r3, uint32_t n)
{
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t t3 = sextract64(r3, 0, 32);
    int64_t mul, ret;

    if ((t2 == -0x8000ll) && (t3 == -0x8000ll) && (n == 1)) {
        mul = 0x7fffffff;
    } else {
        mul = (t2 * t3) << n;
    }

    ret = t1 + mul + 0x8000;

    env->PSW_USB_AV = ret ^ ret * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    if (ret > 0x7fffffffll) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV |= env->PSW_USB_V;
        ret = INT32_MAX;
    } else if (ret < -0x80000000ll) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV |= env->PSW_USB_V;
        ret = INT32_MIN;
    } else {
        env->PSW_USB_V = 0;
    }
    return ret & 0xffff0000ll;
}

uint64_t helper_madd64_suov(CPUTriCoreState *env, target_ulong r1,
                            uint64_t r2, target_ulong r3)
{
    uint64_t ret, mul;
    uint64_t t1 = extract64(r1, 0, 32);
    uint64_t t3 = extract64(r3, 0, 32);

    mul = t1 * t3;
    ret = mul + r2;

    t1 = ret >> 32;
    env->PSW_USB_AV = t1 ^ t1 * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    if (ret < r2) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV = (1 << 31);
        /* saturate */
        ret = UINT64_MAX;
    } else {
        env->PSW_USB_V = 0;
    }
    return ret;
}

target_ulong helper_msub32_ssov(CPUTriCoreState *env, target_ulong r1,
                                target_ulong r2, target_ulong r3)
{
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t t3 = sextract64(r3, 0, 32);
    int64_t result;

    result = t2 - (t1 * t3);
    return ssov32(env, result);
}

target_ulong helper_msub32_suov(CPUTriCoreState *env, target_ulong r1,
                                target_ulong r2, target_ulong r3)
{
    uint64_t t1 = extract64(r1, 0, 32);
    uint64_t t2 = extract64(r2, 0, 32);
    uint64_t t3 = extract64(r3, 0, 32);
    uint64_t result;
    uint64_t mul;

    mul = (t1 * t3);
    result = t2 - mul;

    env->PSW_USB_AV = result ^ result * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;
    /* we calculate ovf by hand here, because the multiplication can overflow on
       the host, which would give false results if we compare to less than
       zero */
    if (mul > t2) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV = (1 << 31);
        result = 0;
    } else {
        env->PSW_USB_V = 0;
    }
    return result;
}

uint64_t helper_msub64_ssov(CPUTriCoreState *env, target_ulong r1,
                            uint64_t r2, target_ulong r3)
{
    uint64_t ret, ovf;
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t3 = sextract64(r3, 0, 32);
    int64_t mul;

    mul = t1 * t3;
    ret = r2 - mul;
    ovf = (ret ^ r2) & (mul ^ r2);

    t1 = ret >> 32;
    env->PSW_USB_AV = t1 ^ t1 * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    if ((int64_t)ovf < 0) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV = (1 << 31);
        /* ext_ret > MAX_INT */
        if (mul < 0) {
            ret = INT64_MAX;
        /* ext_ret < MIN_INT */
        } else {
            ret = INT64_MIN;
        }
    } else {
        env->PSW_USB_V = 0;
    }
    return ret;
}

uint64_t helper_msub64_suov(CPUTriCoreState *env, target_ulong r1,
                            uint64_t r2, target_ulong r3)
{
    uint64_t ret, mul;
    uint64_t t1 = extract64(r1, 0, 32);
    uint64_t t3 = extract64(r3, 0, 32);

    mul = t1 * t3;
    ret = r2 - mul;

    t1 = ret >> 32;
    env->PSW_USB_AV = t1 ^ t1 * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    if (ret > r2) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV = (1 << 31);
        /* saturate */
        ret = 0;
    } else {
        env->PSW_USB_V = 0;
    }
    return ret;
}

uint32_t
helper_msub32_q_sub_ssov(CPUTriCoreState *env, uint64_t r1, uint64_t r2)
{
    int64_t result;
    int64_t t1 = (int64_t)r1;
    int64_t t2 = (int64_t)r2;

    result = t1 - t2;

    env->PSW_USB_AV = (result ^ result * 2u);
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    /* we do the saturation by hand, since we produce an overflow on the host
       if the mul before was (0x80000000 * 0x80000000) << 1). If this is the
       case, we flip the saturated value. */
    if (r2 == 0x8000000000000000LL) {
        if (result > 0x7fffffffLL) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            result = INT32_MIN;
        } else if (result < -0x80000000LL) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            result = INT32_MAX;
        } else {
            env->PSW_USB_V = 0;
        }
    } else {
        if (result > 0x7fffffffLL) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            result = INT32_MAX;
        } else if (result < -0x80000000LL) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            result = INT32_MIN;
        } else {
            env->PSW_USB_V = 0;
        }
    }
    return (uint32_t)result;
}

uint64_t helper_msub64_q_ssov(CPUTriCoreState *env, uint64_t r1, uint32_t r2,
                              uint32_t r3, uint32_t n)
{
    int64_t t1 = (int64_t)r1;
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t t3 = sextract64(r3, 0, 32);
    int64_t result, mul;
    int64_t ovf;

    mul = (t2 * t3) << n;
    result = t1 - mul;

    env->PSW_USB_AV = (result ^ result * 2u) >> 32;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    ovf = (result ^ t1) & (t1 ^ mul);
    /* we do the saturation by hand, since we produce an overflow on the host
       if the mul before was (0x80000000 * 0x80000000) << 1). If this is the
       case, we flip the saturated value. */
    if (mul == 0x8000000000000000LL) {
        if (ovf >= 0) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            /* ext_ret > MAX_INT */
            if (mul >= 0) {
                result = INT64_MAX;
            /* ext_ret < MIN_INT */
            } else {
               result = INT64_MIN;
            }
        } else {
            env->PSW_USB_V = 0;
        }
    } else {
        if (ovf < 0) {
            env->PSW_USB_V = (1 << 31);
            env->PSW_USB_SV = (1 << 31);
            /* ext_ret > MAX_INT */
            if (mul < 0) {
                result = INT64_MAX;
            /* ext_ret < MIN_INT */
            } else {
               result = INT64_MIN;
            }
        } else {
            env->PSW_USB_V = 0;
        }
    }

    return (uint64_t)result;
}

uint32_t helper_msubr_q_ssov(CPUTriCoreState *env, uint32_t r1, uint32_t r2,
                             uint32_t r3, uint32_t n)
{
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t t3 = sextract64(r3, 0, 32);
    int64_t mul, ret;

    if ((t2 == -0x8000ll) && (t3 == -0x8000ll) && (n == 1)) {
        mul = 0x7fffffff;
    } else {
        mul = (t2 * t3) << n;
    }

    ret = t1 - mul + 0x8000;

    env->PSW_USB_AV = ret ^ ret * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    if (ret > 0x7fffffffll) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV |= env->PSW_USB_V;
        ret = INT32_MAX;
    } else if (ret < -0x80000000ll) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV |= env->PSW_USB_V;
        ret = INT32_MIN;
    } else {
        env->PSW_USB_V = 0;
    }
    return ret & 0xffff0000ll;
}

uint32_t helper_abs_b(CPUTriCoreState *env, target_ulong arg)
{
    int32_t b, i;
    int32_t ovf = 0;
    int32_t avf = 0;
    int32_t ret = 0;

    for (i = 0; i < 4; i++) {
        b = sextract32(arg, i * 8, 8);
        b = (b >= 0) ? b : (0 - b);
        ovf |= (b > 0x7F) || (b < -0x80);
        avf |= b ^ b * 2u;
        ret |= (b & 0xff) << (i * 8);
    }

    env->PSW_USB_V = ovf << 31;
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = avf << 24;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return ret;
}

uint32_t helper_abs_h(CPUTriCoreState *env, target_ulong arg)
{
    int32_t h, i;
    int32_t ovf = 0;
    int32_t avf = 0;
    int32_t ret = 0;

    for (i = 0; i < 2; i++) {
        h = sextract32(arg, i * 16, 16);
        h = (h >= 0) ? h : (0 - h);
        ovf |= (h > 0x7FFF) || (h < -0x8000);
        avf |= h ^ h * 2u;
        ret |= (h & 0xffff) << (i * 16);
    }

    env->PSW_USB_V = ovf << 31;
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = avf << 16;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return ret;
}

uint32_t helper_absdif_b(CPUTriCoreState *env, target_ulong r1, target_ulong r2)
{
    int32_t b, i;
    int32_t extr_r2;
    int32_t ovf = 0;
    int32_t avf = 0;
    int32_t ret = 0;

    for (i = 0; i < 4; i++) {
        extr_r2 = sextract32(r2, i * 8, 8);
        b = sextract32(r1, i * 8, 8);
        b = (b > extr_r2) ? (b - extr_r2) : (extr_r2 - b);
        ovf |= (b > 0x7F) || (b < -0x80);
        avf |= b ^ b * 2u;
        ret |= (b & 0xff) << (i * 8);
    }

    env->PSW_USB_V = ovf << 31;
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = avf << 24;
    env->PSW_USB_SAV |= env->PSW_USB_AV;
    return ret;
}

uint32_t helper_absdif_h(CPUTriCoreState *env, target_ulong r1, target_ulong r2)
{
    int32_t h, i;
    int32_t extr_r2;
    int32_t ovf = 0;
    int32_t avf = 0;
    int32_t ret = 0;

    for (i = 0; i < 2; i++) {
        extr_r2 = sextract32(r2, i * 16, 16);
        h = sextract32(r1, i * 16, 16);
        h = (h > extr_r2) ? (h - extr_r2) : (extr_r2 - h);
        ovf |= (h > 0x7FFF) || (h < -0x8000);
        avf |= h ^ h * 2u;
        ret |= (h & 0xffff) << (i * 16);
    }

    env->PSW_USB_V = ovf << 31;
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = avf << 16;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return ret;
}

uint32_t helper_addr_h(CPUTriCoreState *env, uint64_t r1, uint32_t r2_l,
                       uint32_t r2_h)
{
    int64_t mul_res0 = sextract64(r1, 0, 32);
    int64_t mul_res1 = sextract64(r1, 32, 32);
    int64_t r2_low = sextract64(r2_l, 0, 32);
    int64_t r2_high = sextract64(r2_h, 0, 32);
    int64_t result0, result1;
    uint32_t ovf0, ovf1;
    uint32_t avf0, avf1;

    ovf0 = ovf1 = 0;

    result0 = r2_low + mul_res0 + 0x8000;
    result1 = r2_high + mul_res1 + 0x8000;

    if ((result0 > INT32_MAX) || (result0 < INT32_MIN)) {
        ovf0 = (1 << 31);
    }

    if ((result1 > INT32_MAX) || (result1 < INT32_MIN)) {
        ovf1 = (1 << 31);
    }

    env->PSW_USB_V = ovf0 | ovf1;
    env->PSW_USB_SV |= env->PSW_USB_V;

    avf0 = result0 * 2u;
    avf0 = result0 ^ avf0;
    avf1 = result1 * 2u;
    avf1 = result1 ^ avf1;

    env->PSW_USB_AV = avf0 | avf1;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return (result1 & 0xffff0000ULL) | ((result0 >> 16) & 0xffffULL);
}

uint32_t helper_addsur_h(CPUTriCoreState *env, uint64_t r1, uint32_t r2_l,
                         uint32_t r2_h)
{
    int64_t mul_res0 = sextract64(r1, 0, 32);
    int64_t mul_res1 = sextract64(r1, 32, 32);
    int64_t r2_low = sextract64(r2_l, 0, 32);
    int64_t r2_high = sextract64(r2_h, 0, 32);
    int64_t result0, result1;
    uint32_t ovf0, ovf1;
    uint32_t avf0, avf1;

    ovf0 = ovf1 = 0;

    result0 = r2_low - mul_res0 + 0x8000;
    result1 = r2_high + mul_res1 + 0x8000;

    if ((result0 > INT32_MAX) || (result0 < INT32_MIN)) {
        ovf0 = (1 << 31);
    }

    if ((result1 > INT32_MAX) || (result1 < INT32_MIN)) {
        ovf1 = (1 << 31);
    }

    env->PSW_USB_V = ovf0 | ovf1;
    env->PSW_USB_SV |= env->PSW_USB_V;

    avf0 = result0 * 2u;
    avf0 = result0 ^ avf0;
    avf1 = result1 * 2u;
    avf1 = result1 ^ avf1;

    env->PSW_USB_AV = avf0 | avf1;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return (result1 & 0xffff0000ULL) | ((result0 >> 16) & 0xffffULL);
}

uint32_t helper_maddr_q(CPUTriCoreState *env, uint32_t r1, uint32_t r2,
                        uint32_t r3, uint32_t n)
{
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t t3 = sextract64(r3, 0, 32);
    int64_t mul, ret;

    if ((t2 == -0x8000ll) && (t3 == -0x8000ll) && (n == 1)) {
        mul = 0x7fffffff;
    } else {
        mul = (t2 * t3) << n;
    }

    ret = t1 + mul + 0x8000;

    if ((ret > 0x7fffffffll) || (ret < -0x80000000ll)) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV |= env->PSW_USB_V;
    } else {
        env->PSW_USB_V = 0;
    }
    env->PSW_USB_AV = ret ^ ret * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return ret & 0xffff0000ll;
}

uint32_t helper_add_b(CPUTriCoreState *env, target_ulong r1, target_ulong r2)
{
    int32_t b, i;
    int32_t extr_r1, extr_r2;
    int32_t ovf = 0;
    int32_t avf = 0;
    uint32_t ret = 0;

    for (i = 0; i < 4; i++) {
        extr_r1 = sextract32(r1, i * 8, 8);
        extr_r2 = sextract32(r2, i * 8, 8);

        b = extr_r1 + extr_r2;
        ovf |= ((b > 0x7f) || (b < -0x80));
        avf |= b ^ b * 2u;
        ret |= ((b & 0xff) << (i*8));
    }

    env->PSW_USB_V = (ovf << 31);
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = avf << 24;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return ret;
}

uint32_t helper_add_h(CPUTriCoreState *env, target_ulong r1, target_ulong r2)
{
    int32_t h, i;
    int32_t extr_r1, extr_r2;
    int32_t ovf = 0;
    int32_t avf = 0;
    int32_t ret = 0;

    for (i = 0; i < 2; i++) {
        extr_r1 = sextract32(r1, i * 16, 16);
        extr_r2 = sextract32(r2, i * 16, 16);
        h = extr_r1 + extr_r2;
        ovf |= ((h > 0x7fff) || (h < -0x8000));
        avf |= h ^ h * 2u;
        ret |= (h & 0xffff) << (i * 16);
    }

    env->PSW_USB_V = (ovf << 31);
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = (avf << 16);
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return ret;
}

uint32_t helper_subr_h(CPUTriCoreState *env, uint64_t r1, uint32_t r2_l,
                       uint32_t r2_h)
{
    int64_t mul_res0 = sextract64(r1, 0, 32);
    int64_t mul_res1 = sextract64(r1, 32, 32);
    int64_t r2_low = sextract64(r2_l, 0, 32);
    int64_t r2_high = sextract64(r2_h, 0, 32);
    int64_t result0, result1;
    uint32_t ovf0, ovf1;
    uint32_t avf0, avf1;

    ovf0 = ovf1 = 0;

    result0 = r2_low - mul_res0 + 0x8000;
    result1 = r2_high - mul_res1 + 0x8000;

    if ((result0 > INT32_MAX) || (result0 < INT32_MIN)) {
        ovf0 = (1 << 31);
    }

    if ((result1 > INT32_MAX) || (result1 < INT32_MIN)) {
        ovf1 = (1 << 31);
    }

    env->PSW_USB_V = ovf0 | ovf1;
    env->PSW_USB_SV |= env->PSW_USB_V;

    avf0 = result0 * 2u;
    avf0 = result0 ^ avf0;
    avf1 = result1 * 2u;
    avf1 = result1 ^ avf1;

    env->PSW_USB_AV = avf0 | avf1;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return (result1 & 0xffff0000ULL) | ((result0 >> 16) & 0xffffULL);
}

uint32_t helper_subadr_h(CPUTriCoreState *env, uint64_t r1, uint32_t r2_l,
                         uint32_t r2_h)
{
    int64_t mul_res0 = sextract64(r1, 0, 32);
    int64_t mul_res1 = sextract64(r1, 32, 32);
    int64_t r2_low = sextract64(r2_l, 0, 32);
    int64_t r2_high = sextract64(r2_h, 0, 32);
    int64_t result0, result1;
    uint32_t ovf0, ovf1;
    uint32_t avf0, avf1;

    ovf0 = ovf1 = 0;

    result0 = r2_low + mul_res0 + 0x8000;
    result1 = r2_high - mul_res1 + 0x8000;

    if ((result0 > INT32_MAX) || (result0 < INT32_MIN)) {
        ovf0 = (1 << 31);
    }

    if ((result1 > INT32_MAX) || (result1 < INT32_MIN)) {
        ovf1 = (1 << 31);
    }

    env->PSW_USB_V = ovf0 | ovf1;
    env->PSW_USB_SV |= env->PSW_USB_V;

    avf0 = result0 * 2u;
    avf0 = result0 ^ avf0;
    avf1 = result1 * 2u;
    avf1 = result1 ^ avf1;

    env->PSW_USB_AV = avf0 | avf1;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return (result1 & 0xffff0000ULL) | ((result0 >> 16) & 0xffffULL);
}

uint32_t helper_msubr_q(CPUTriCoreState *env, uint32_t r1, uint32_t r2,
                        uint32_t r3, uint32_t n)
{
    int64_t t1 = sextract64(r1, 0, 32);
    int64_t t2 = sextract64(r2, 0, 32);
    int64_t t3 = sextract64(r3, 0, 32);
    int64_t mul, ret;

    if ((t2 == -0x8000ll) && (t3 == -0x8000ll) && (n == 1)) {
        mul = 0x7fffffff;
    } else {
        mul = (t2 * t3) << n;
    }

    ret = t1 - mul + 0x8000;

    if ((ret > 0x7fffffffll) || (ret < -0x80000000ll)) {
        env->PSW_USB_V = (1 << 31);
        env->PSW_USB_SV |= env->PSW_USB_V;
    } else {
        env->PSW_USB_V = 0;
    }
    env->PSW_USB_AV = ret ^ ret * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return ret & 0xffff0000ll;
}

uint32_t helper_sub_b(CPUTriCoreState *env, target_ulong r1, target_ulong r2)
{
    int32_t b, i;
    int32_t extr_r1, extr_r2;
    int32_t ovf = 0;
    int32_t avf = 0;
    uint32_t ret = 0;

    for (i = 0; i < 4; i++) {
        extr_r1 = sextract32(r1, i * 8, 8);
        extr_r2 = sextract32(r2, i * 8, 8);

        b = extr_r1 - extr_r2;
        ovf |= ((b > 0x7f) || (b < -0x80));
        avf |= b ^ b * 2u;
        ret |= ((b & 0xff) << (i*8));
    }

    env->PSW_USB_V = (ovf << 31);
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = avf << 24;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return ret;
}

uint32_t helper_sub_h(CPUTriCoreState *env, target_ulong r1, target_ulong r2)
{
    int32_t h, i;
    int32_t extr_r1, extr_r2;
    int32_t ovf = 0;
    int32_t avf = 0;
    int32_t ret = 0;

    for (i = 0; i < 2; i++) {
        extr_r1 = sextract32(r1, i * 16, 16);
        extr_r2 = sextract32(r2, i * 16, 16);
        h = extr_r1 - extr_r2;
        ovf |= ((h > 0x7fff) || (h < -0x8000));
        avf |= h ^ h * 2u;
        ret |= (h & 0xffff) << (i * 16);
    }

    env->PSW_USB_V = (ovf << 31);
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = avf << 16;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return ret;
}

uint32_t helper_eq_b(target_ulong r1, target_ulong r2)
{
    int32_t ret;
    int32_t i, msk;

    ret = 0;
    msk = 0xff;
    for (i = 0; i < 4; i++) {
        if ((r1 & msk) == (r2 & msk)) {
            ret |= msk;
        }
        msk = msk << 8;
    }

    return ret;
}

uint32_t helper_eq_h(target_ulong r1, target_ulong r2)
{
    int32_t ret = 0;

    if ((r1 & 0xffff) == (r2 & 0xffff)) {
        ret = 0xffff;
    }

    if ((r1 & 0xffff0000) == (r2 & 0xffff0000)) {
        ret |= 0xffff0000;
    }

    return ret;
}

uint32_t helper_eqany_b(target_ulong r1, target_ulong r2)
{
    int32_t i;
    uint32_t ret = 0;

    for (i = 0; i < 4; i++) {
        ret |= (sextract32(r1,  i * 8, 8) == sextract32(r2,  i * 8, 8));
    }

    return ret;
}

uint32_t helper_eqany_h(target_ulong r1, target_ulong r2)
{
    uint32_t ret;

    ret = (sextract32(r1, 0, 16) == sextract32(r2,  0, 16));
    ret |= (sextract32(r1, 16, 16) == sextract32(r2,  16, 16));

    return ret;
}

uint32_t helper_lt_b(target_ulong r1, target_ulong r2)
{
    int32_t i;
    uint32_t ret = 0;

    for (i = 0; i < 4; i++) {
        if (sextract32(r1,  i * 8, 8) < sextract32(r2,  i * 8, 8)) {
            ret |= (0xff << (i * 8));
        }
    }

    return ret;
}

uint32_t helper_lt_bu(target_ulong r1, target_ulong r2)
{
    int32_t i;
    uint32_t ret = 0;

    for (i = 0; i < 4; i++) {
        if (extract32(r1,  i * 8, 8) < extract32(r2,  i * 8, 8)) {
            ret |= (0xff << (i * 8));
        }
    }

    return ret;
}

uint32_t helper_lt_h(target_ulong r1, target_ulong r2)
{
    uint32_t ret = 0;

    if (sextract32(r1,  0, 16) < sextract32(r2,  0, 16)) {
        ret |= 0xffff;
    }

    if (sextract32(r1,  16, 16) < sextract32(r2,  16, 16)) {
        ret |= 0xffff0000;
    }

    return ret;
}

uint32_t helper_lt_hu(target_ulong r1, target_ulong r2)
{
    uint32_t ret = 0;

    if (extract32(r1,  0, 16) < extract32(r2,  0, 16)) {
        ret |= 0xffff;
    }

    if (extract32(r1,  16, 16) < extract32(r2,  16, 16)) {
        ret |= 0xffff0000;
    }

    return ret;
}

#define EXTREMA_H_B(name, op)                                 \
uint32_t helper_##name ##_b(target_ulong r1, target_ulong r2) \
{                                                             \
    int32_t i, extr_r1, extr_r2;                              \
    uint32_t ret = 0;                                         \
                                                              \
    for (i = 0; i < 4; i++) {                                 \
        extr_r1 = sextract32(r1, i * 8, 8);                   \
        extr_r2 = sextract32(r2, i * 8, 8);                   \
        extr_r1 = (extr_r1 op extr_r2) ? extr_r1 : extr_r2;   \
        ret |= (extr_r1 & 0xff) << (i * 8);                   \
    }                                                         \
    return ret;                                               \
}                                                             \
                                                              \
uint32_t helper_##name ##_bu(target_ulong r1, target_ulong r2)\
{                                                             \
    int32_t i;                                                \
    uint32_t extr_r1, extr_r2;                                \
    uint32_t ret = 0;                                         \
                                                              \
    for (i = 0; i < 4; i++) {                                 \
        extr_r1 = extract32(r1, i * 8, 8);                    \
        extr_r2 = extract32(r2, i * 8, 8);                    \
        extr_r1 = (extr_r1 op extr_r2) ? extr_r1 : extr_r2;   \
        ret |= (extr_r1 & 0xff) << (i * 8);                   \
    }                                                         \
    return ret;                                               \
}                                                             \
                                                              \
uint32_t helper_##name ##_h(target_ulong r1, target_ulong r2) \
{                                                             \
    int32_t extr_r1, extr_r2;                                 \
    uint32_t ret = 0;                                         \
                                                              \
    extr_r1 = sextract32(r1, 0, 16);                          \
    extr_r2 = sextract32(r2, 0, 16);                          \
    ret = (extr_r1 op extr_r2) ? extr_r1 : extr_r2;           \
    ret = ret & 0xffff;                                       \
                                                              \
    extr_r1 = sextract32(r1, 16, 16);                         \
    extr_r2 = sextract32(r2, 16, 16);                         \
    extr_r1 = (extr_r1 op extr_r2) ? extr_r1 : extr_r2;       \
    ret |= extr_r1 << 16;                                     \
                                                              \
    return ret;                                               \
}                                                             \
                                                              \
uint32_t helper_##name ##_hu(target_ulong r1, target_ulong r2)\
{                                                             \
    uint32_t extr_r1, extr_r2;                                \
    uint32_t ret = 0;                                         \
                                                              \
    extr_r1 = extract32(r1, 0, 16);                           \
    extr_r2 = extract32(r2, 0, 16);                           \
    ret = (extr_r1 op extr_r2) ? extr_r1 : extr_r2;           \
    ret = ret & 0xffff;                                       \
                                                              \
    extr_r1 = extract32(r1, 16, 16);                          \
    extr_r2 = extract32(r2, 16, 16);                          \
    extr_r1 = (extr_r1 op extr_r2) ? extr_r1 : extr_r2;       \
    ret |= extr_r1 << (16);                                   \
                                                              \
    return ret;                                               \
}                                                             \
                                                              \
uint64_t helper_ix##name(uint64_t r1, uint32_t r2)            \
{                                                             \
    int64_t r2l, r2h, r1hl;                                   \
    uint64_t ret = 0;                                         \
                                                              \
    ret = ((r1 + 2) & 0xffff);                                \
    r2l = sextract64(r2, 0, 16);                              \
    r2h = sextract64(r2, 16, 16);                             \
    r1hl = sextract64(r1, 32, 16);                            \
                                                              \
    if ((r2l op ## = r2h) && (r2l op r1hl)) {                 \
        ret |= (r2l & 0xffff) << 32;                          \
        ret |= extract64(r1, 0, 16) << 16;                    \
    } else if ((r2h op r2l) && (r2h op r1hl)) {               \
        ret |= extract64(r2, 16, 16) << 32;                   \
        ret |= extract64(r1 + 1, 0, 16) << 16;                \
    } else {                                                  \
        ret |= r1 & 0xffffffff0000ull;                        \
    }                                                         \
    return ret;                                               \
}                                                             \
                                                              \
uint64_t helper_ix##name ##_u(uint64_t r1, uint32_t r2)       \
{                                                             \
    int64_t r2l, r2h, r1hl;                                   \
    uint64_t ret = 0;                                         \
                                                              \
    ret = ((r1 + 2) & 0xffff);                                \
    r2l = extract64(r2, 0, 16);                               \
    r2h = extract64(r2, 16, 16);                              \
    r1hl = extract64(r1, 32, 16);                             \
                                                              \
    if ((r2l op ## = r2h) && (r2l op r1hl)) {                 \
        ret |= (r2l & 0xffff) << 32;                          \
        ret |= extract64(r1, 0, 16) << 16;                    \
    } else if ((r2h op r2l) && (r2h op r1hl)) {               \
        ret |= extract64(r2, 16, 16) << 32;                   \
        ret |= extract64(r1 + 1, 0, 16) << 16;                \
    } else {                                                  \
        ret |= r1 & 0xffffffff0000ull;                        \
    }                                                         \
    return ret;                                               \
}

EXTREMA_H_B(max, >)
EXTREMA_H_B(min, <)

#undef EXTREMA_H_B

uint32_t helper_clo(target_ulong r1)
{
    return clo32(r1);
}

uint32_t helper_clo_h(target_ulong r1)
{
    uint32_t ret_hw0 = extract32(r1, 0, 16);
    uint32_t ret_hw1 = extract32(r1, 16, 16);

    ret_hw0 = clo32(ret_hw0 << 16);
    ret_hw1 = clo32(ret_hw1 << 16);

    if (ret_hw0 > 16) {
        ret_hw0 = 16;
    }
    if (ret_hw1 > 16) {
        ret_hw1 = 16;
    }

    return ret_hw0 | (ret_hw1 << 16);
}

uint32_t helper_clz(target_ulong r1)
{
    return clz32(r1);
}

uint32_t helper_clz_h(target_ulong r1)
{
    uint32_t ret_hw0 = extract32(r1, 0, 16);
    uint32_t ret_hw1 = extract32(r1, 16, 16);

    ret_hw0 = clz32(ret_hw0 << 16);
    ret_hw1 = clz32(ret_hw1 << 16);

    if (ret_hw0 > 16) {
        ret_hw0 = 16;
    }
    if (ret_hw1 > 16) {
        ret_hw1 = 16;
    }

    return ret_hw0 | (ret_hw1 << 16);
}

uint32_t helper_cls(target_ulong r1)
{
    return clrsb32(r1);
}

uint32_t helper_cls_h(target_ulong r1)
{
    uint32_t ret_hw0 = extract32(r1, 0, 16);
    uint32_t ret_hw1 = extract32(r1, 16, 16);

    ret_hw0 = clrsb32(ret_hw0 << 16);
    ret_hw1 = clrsb32(ret_hw1 << 16);

    if (ret_hw0 > 15) {
        ret_hw0 = 15;
    }
    if (ret_hw1 > 15) {
        ret_hw1 = 15;
    }

    return ret_hw0 | (ret_hw1 << 16);
}

uint32_t helper_sh(target_ulong r1, target_ulong r2)
{
    int32_t shift_count = sextract32(r2, 0, 6);

    if (shift_count == -32) {
        return 0;
    } else if (shift_count < 0) {
        return r1 >> -shift_count;
    } else {
        return r1 << shift_count;
    }
}

uint32_t helper_sh_h(target_ulong r1, target_ulong r2)
{
    int32_t ret_hw0, ret_hw1;
    int32_t shift_count;

    shift_count = sextract32(r2, 0, 5);

    if (shift_count == -16) {
        return 0;
    } else if (shift_count < 0) {
        ret_hw0 = extract32(r1, 0, 16) >> -shift_count;
        ret_hw1 = extract32(r1, 16, 16) >> -shift_count;
        return (ret_hw0 & 0xffff) | (ret_hw1 << 16);
    } else {
        ret_hw0 = extract32(r1, 0, 16) << shift_count;
        ret_hw1 = extract32(r1, 16, 16) << shift_count;
        return (ret_hw0 & 0xffff) | (ret_hw1 << 16);
    }
}

uint32_t helper_sha(CPUTriCoreState *env, target_ulong r1, target_ulong r2)
{
    int32_t shift_count;
    int64_t result, t1;
    uint32_t ret;

    shift_count = sextract32(r2, 0, 6);
    t1 = sextract32(r1, 0, 32);

    if (shift_count == 0) {
        env->PSW_USB_C = env->PSW_USB_V = 0;
        ret = r1;
    } else if (shift_count == -32) {
        env->PSW_USB_C = r1;
        env->PSW_USB_V = 0;
        ret = t1 >> 31;
    } else if (shift_count > 0) {
        result = t1 << shift_count;
        /* calc carry */
        env->PSW_USB_C = ((result & 0xffffffff00000000ULL) != 0);
        /* calc v */
        env->PSW_USB_V = (((result > 0x7fffffffLL) ||
                           (result < -0x80000000LL)) << 31);
        /* calc sv */
        env->PSW_USB_SV |= env->PSW_USB_V;
        ret = (uint32_t)result;
    } else {
        env->PSW_USB_V = 0;
        env->PSW_USB_C = (r1 & ((1 << -shift_count) - 1));
        ret = t1 >> -shift_count;
    }

    env->PSW_USB_AV = ret ^ ret * 2u;
    env->PSW_USB_SAV |= env->PSW_USB_AV;

    return ret;
}

uint32_t helper_sha_h(target_ulong r1, target_ulong r2)
{
    int32_t shift_count;
    int32_t ret_hw0, ret_hw1;

    shift_count = sextract32(r2, 0, 5);

    if (shift_count == 0) {
        return r1;
    } else if (shift_count < 0) {
        ret_hw0 = sextract32(r1, 0, 16) >> -shift_count;
        ret_hw1 = sextract32(r1, 16, 16) >> -shift_count;
        return (ret_hw0 & 0xffff) | (ret_hw1 << 16);
    } else {
        ret_hw0 = sextract32(r1, 0, 16) << shift_count;
        ret_hw1 = sextract32(r1, 16, 16) << shift_count;
        return (ret_hw0 & 0xffff) | (ret_hw1 << 16);
    }
}

uint32_t helper_bmerge(target_ulong r1, target_ulong r2)
{
    uint32_t i, ret;

    ret = 0;
    for (i = 0; i < 16; i++) {
        ret |= (r1 & 1) << (2 * i + 1);
        ret |= (r2 & 1) << (2 * i);
        r1 = r1 >> 1;
        r2 = r2 >> 1;
    }
    return ret;
}

uint64_t helper_bsplit(uint32_t r1)
{
    int32_t i;
    uint64_t ret;

    ret = 0;
    for (i = 0; i < 32; i = i + 2) {
        /* even */
        ret |= (r1 & 1) << (i/2);
        r1 = r1 >> 1;
        /* odd */
        ret |= (uint64_t)(r1 & 1) << (i/2 + 32);
        r1 = r1 >> 1;
    }
    return ret;
}

uint32_t helper_parity(target_ulong r1)
{
    uint32_t ret;
    uint32_t nOnes, i;

    ret = 0;
    nOnes = 0;
    for (i = 0; i < 8; i++) {
        ret ^= (r1 & 1);
        r1 = r1 >> 1;
    }
    /* second byte */
    nOnes = 0;
    for (i = 0; i < 8; i++) {
        nOnes ^= (r1 & 1);
        r1 = r1 >> 1;
    }
    ret |= nOnes << 8;
    /* third byte */
    nOnes = 0;
    for (i = 0; i < 8; i++) {
        nOnes ^= (r1 & 1);
        r1 = r1 >> 1;
    }
    ret |= nOnes << 16;
    /* fourth byte */
    nOnes = 0;
    for (i = 0; i < 8; i++) {
        nOnes ^= (r1 & 1);
        r1 = r1 >> 1;
    }
    ret |= nOnes << 24;

    return ret;
}

uint32_t helper_pack(uint32_t carry, uint32_t r1_low, uint32_t r1_high,
                     target_ulong r2)
{
    uint32_t ret;
    int32_t fp_exp, fp_frac, temp_exp, fp_exp_frac;
    int32_t int_exp  = r1_high;
    int32_t int_mant = r1_low;
    uint32_t flag_rnd = (int_mant & (1 << 7)) && (
                        (int_mant & (1 << 8)) ||
                        (int_mant & 0x7f)     ||
                        (carry != 0));
    if (((int_mant & (1<<31)) == 0) && (int_exp == 255)) {
        fp_exp = 255;
        fp_frac = extract32(int_mant, 8, 23);
    } else if ((int_mant & (1<<31)) && (int_exp >= 127)) {
        fp_exp  = 255;
        fp_frac = 0;
    } else if ((int_mant & (1<<31)) && (int_exp <= -128)) {
        fp_exp  = 0;
        fp_frac = 0;
    } else if (int_mant == 0) {
        fp_exp  = 0;
        fp_frac = 0;
    } else {
        if (((int_mant & (1 << 31)) == 0)) {
            temp_exp = 0;
        } else {
            temp_exp = int_exp + 128;
        }
        fp_exp_frac = (((temp_exp & 0xff) << 23) |
                      extract32(int_mant, 8, 23))
                      + flag_rnd;
        fp_exp  = extract32(fp_exp_frac, 23, 8);
        fp_frac = extract32(fp_exp_frac, 0, 23);
    }
    ret = r2 & (1 << 31);
    ret = ret + (fp_exp << 23);
    ret = ret + (fp_frac & 0x7fffff);

    return ret;
}

uint64_t helper_unpack(target_ulong arg1)
{
    int32_t fp_exp  = extract32(arg1, 23, 8);
    int32_t fp_frac = extract32(arg1, 0, 23);
    uint64_t ret;
    int32_t int_exp, int_mant;

    if (fp_exp == 255) {
        int_exp = 255;
        int_mant = (fp_frac << 7);
    } else if ((fp_exp == 0) && (fp_frac == 0)) {
        int_exp  = -127;
        int_mant = 0;
    } else if ((fp_exp == 0) && (fp_frac != 0)) {
        int_exp  = -126;
        int_mant = (fp_frac << 7);
    } else {
        int_exp  = fp_exp - 127;
        int_mant = (fp_frac << 7);
        int_mant |= (1 << 30);
    }
    ret = int_exp;
    ret = ret << 32;
    ret |= int_mant;

    return ret;
}

uint64_t helper_dvinit_b_13(CPUTriCoreState *env, uint32_t r1, uint32_t r2)
{
    uint64_t ret;
    int32_t abs_sig_dividend, abs_divisor;

    ret = sextract32(r1, 0, 32);
    ret = ret << 24;
    if (!((r1 & 0x80000000) == (r2 & 0x80000000))) {
        ret |= 0xffffff;
    }

    abs_sig_dividend = abs((int32_t)r1) >> 8;
    abs_divisor = abs((int32_t)r2);
    /* calc overflow
       ofv if (a/b >= 255) <=> (a/255 >= b) */
    env->PSW_USB_V = (abs_sig_dividend >= abs_divisor) << 31;
    env->PSW_USB_V = env->PSW_USB_V << 31;
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = 0;

    return ret;
}

uint64_t helper_dvinit_b_131(CPUTriCoreState *env, uint32_t r1, uint32_t r2)
{
    uint64_t ret = sextract32(r1, 0, 32);

    ret = ret << 24;
    if (!((r1 & 0x80000000) == (r2 & 0x80000000))) {
        ret |= 0xffffff;
    }
    /* calc overflow */
    env->PSW_USB_V = ((r2 == 0) || ((r2 == 0xffffffff) && (r1 == 0xffffff80)));
    env->PSW_USB_V = env->PSW_USB_V << 31;
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = 0;

    return ret;
}

uint64_t helper_dvinit_h_13(CPUTriCoreState *env, uint32_t r1, uint32_t r2)
{
    uint64_t ret;
    int32_t abs_sig_dividend, abs_divisor;

    ret = sextract32(r1, 0, 32);
    ret = ret << 16;
    if (!((r1 & 0x80000000) == (r2 & 0x80000000))) {
        ret |= 0xffff;
    }

    abs_sig_dividend = abs((int32_t)r1) >> 16;
    abs_divisor = abs((int32_t)r2);
    /* calc overflow
       ofv if (a/b >= 0xffff) <=> (a/0xffff >= b) */
    env->PSW_USB_V = (abs_sig_dividend >= abs_divisor) << 31;
    env->PSW_USB_V = env->PSW_USB_V << 31;
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = 0;

    return ret;
}

uint64_t helper_dvinit_h_131(CPUTriCoreState *env, uint32_t r1, uint32_t r2)
{
    uint64_t ret = sextract32(r1, 0, 32);

    ret = ret << 16;
    if (!((r1 & 0x80000000) == (r2 & 0x80000000))) {
        ret |= 0xffff;
    }
    /* calc overflow */
    env->PSW_USB_V = ((r2 == 0) || ((r2 == 0xffffffff) && (r1 == 0xffff8000)));
    env->PSW_USB_V = env->PSW_USB_V << 31;
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = 0;

    return ret;
}

uint64_t helper_dvadj(uint64_t r1, uint32_t r2)
{
    int32_t x_sign = (r1 >> 63);
    int32_t q_sign = x_sign ^ (r2 >> 31);
    int32_t eq_pos = x_sign & ((r1 >> 32) == r2);
    int32_t eq_neg = x_sign & ((r1 >> 32) == -r2);
    uint32_t quotient;
    uint64_t ret, remainder;

    if ((q_sign & ~eq_neg) | eq_pos) {
        quotient = (r1 + 1) & 0xffffffff;
    } else {
        quotient = r1 & 0xffffffff;
    }

    if (eq_pos | eq_neg) {
        remainder = 0;
    } else {
        remainder = (r1 & 0xffffffff00000000ull);
    }
    ret = remainder|quotient;
    return ret;
}

uint64_t helper_dvstep(uint64_t r1, uint32_t r2)
{
    int32_t dividend_sign = extract64(r1, 63, 1);
    int32_t divisor_sign = extract32(r2, 31, 1);
    int32_t quotient_sign = (dividend_sign != divisor_sign);
    int32_t addend, dividend_quotient, remainder;
    int32_t i, temp;

    if (quotient_sign) {
        addend = r2;
    } else {
        addend = -r2;
    }
    dividend_quotient = (int32_t)r1;
    remainder = (int32_t)(r1 >> 32);

    for (i = 0; i < 8; i++) {
        remainder = (remainder << 1) | extract32(dividend_quotient, 31, 1);
        dividend_quotient <<= 1;
        temp = remainder + addend;
        if ((temp < 0) == dividend_sign) {
            remainder = temp;
        }
        if (((temp < 0) == dividend_sign)) {
            dividend_quotient = dividend_quotient | !quotient_sign;
        } else {
            dividend_quotient = dividend_quotient | quotient_sign;
        }
    }
    return ((uint64_t)remainder << 32) | (uint32_t)dividend_quotient;
}

uint64_t helper_dvstep_u(uint64_t r1, uint32_t r2)
{
    int32_t dividend_quotient = extract64(r1, 0, 32);
    int64_t remainder = extract64(r1, 32, 32);
    int32_t i;
    int64_t temp;
    for (i = 0; i < 8; i++) {
        remainder = (remainder << 1) | extract32(dividend_quotient, 31, 1);
        dividend_quotient <<= 1;
        temp = (remainder & 0xffffffff) - r2;
        if (temp >= 0) {
            remainder = temp;
        }
        dividend_quotient = dividend_quotient | !(temp < 0);
    }
    return ((uint64_t)remainder << 32) | (uint32_t)dividend_quotient;
}

uint64_t helper_divide(CPUTriCoreState *env, uint32_t r1, uint32_t r2)
{
    int32_t quotient, remainder;
    int32_t dividend = (int32_t)r1;
    int32_t divisor = (int32_t)r2;

    if (divisor == 0) {
        if (dividend >= 0) {
            quotient = 0x7fffffff;
            remainder = 0;
        } else {
            quotient = 0x80000000;
            remainder = 0;
        }
        env->PSW_USB_V = (1 << 31);
    } else if ((divisor == 0xffffffff) && (dividend == 0x80000000)) {
        quotient = 0x7fffffff;
        remainder = 0;
        env->PSW_USB_V = (1 << 31);
    } else {
        remainder = dividend % divisor;
        quotient = (dividend - remainder)/divisor;
        env->PSW_USB_V = 0;
    }
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = 0;
    return ((uint64_t)remainder << 32) | (uint32_t)quotient;
}

uint64_t helper_divide_u(CPUTriCoreState *env, uint32_t r1, uint32_t r2)
{
    uint32_t quotient, remainder;
    uint32_t dividend = r1;
    uint32_t divisor = r2;

    if (divisor == 0) {
        quotient = 0xffffffff;
        remainder = 0;
        env->PSW_USB_V = (1 << 31);
    } else {
        remainder = dividend % divisor;
        quotient = (dividend - remainder)/divisor;
        env->PSW_USB_V = 0;
    }
    env->PSW_USB_SV |= env->PSW_USB_V;
    env->PSW_USB_AV = 0;
    return ((uint64_t)remainder << 32) | quotient;
}

uint64_t helper_mul_h(uint32_t arg00, uint32_t arg01,
                      uint32_t arg10, uint32_t arg11, uint32_t n)
{
    uint64_t ret;
    uint32_t result0, result1;

    int32_t sc1 = ((arg00 & 0xffff) == 0x8000) &&
                  ((arg10 & 0xffff) == 0x8000) && (n == 1);
    int32_t sc0 = ((arg01 & 0xffff) == 0x8000) &&
                  ((arg11 & 0xffff) == 0x8000) && (n == 1);
    if (sc1) {
        result1 = 0x7fffffff;
    } else {
        result1 = (((uint32_t)(arg00 * arg10)) << n);
    }
    if (sc0) {
        result0 = 0x7fffffff;
    } else {
        result0 = (((uint32_t)(arg01 * arg11)) << n);
    }
    ret = (((uint64_t)result1 << 32)) | result0;
    return ret;
}

uint64_t helper_mulm_h(uint32_t arg00, uint32_t arg01,
                       uint32_t arg10, uint32_t arg11, uint32_t n)
{
    uint64_t ret;
    int64_t result0, result1;

    int32_t sc1 = ((arg00 & 0xffff) == 0x8000) &&
                  ((arg10 & 0xffff) == 0x8000) && (n == 1);
    int32_t sc0 = ((arg01 & 0xffff) == 0x8000) &&
                  ((arg11 & 0xffff) == 0x8000) && (n == 1);

    if (sc1) {
        result1 = 0x7fffffff;
    } else {
        result1 = (((int32_t)arg00 * (int32_t)arg10) << n);
    }
    if (sc0) {
        result0 = 0x7fffffff;
    } else {
        result0 = (((int32_t)arg01 * (int32_t)arg11) << n);
    }
    ret = (result1 + result0);
    ret = ret << 16;
    return ret;
}
uint32_t helper_mulr_h(uint32_t arg00, uint32_t arg01,
                       uint32_t arg10, uint32_t arg11, uint32_t n)
{
    uint32_t result0, result1;

    int32_t sc1 = ((arg00 & 0xffff) == 0x8000) &&
                  ((arg10 & 0xffff) == 0x8000) && (n == 1);
    int32_t sc0 = ((arg01 & 0xffff) == 0x8000) &&
                  ((arg11 & 0xffff) == 0x8000) && (n == 1);

    if (sc1) {
        result1 = 0x7fffffff;
    } else {
        result1 = ((arg00 * arg10) << n) + 0x8000;
    }
    if (sc0) {
        result0 = 0x7fffffff;
    } else {
        result0 = ((arg01 * arg11) << n) + 0x8000;
    }
    return (result1 & 0xffff0000) | (result0 >> 16);
}

uint32_t helper_crc32(uint32_t arg0, uint32_t arg1)
{
    uint8_t buf[4];
    uint32_t ret;
    stl_be_p(buf, arg0);

    ret = crc32(arg1, buf, 4);
    return ret;
}

/* context save area (CSA) related helpers */

static int cdc_increment(target_ulong *psw)
{
    if ((*psw & MASK_PSW_CDC) == 0x7f) {
        return 0;
    }

    (*psw)++;
    /* check for overflow */
    int lo = clo32((*psw & MASK_PSW_CDC) << (32 - 7));
    int mask = (1u << (7 - lo)) - 1;
    int count = *psw & mask;
    if (count == 0) {
        (*psw)--;
        return 1;
    }
    return 0;
}

static int cdc_decrement(target_ulong *psw)
{
    if ((*psw & MASK_PSW_CDC) == 0x7f) {
        return 0;
    }
    /* check for underflow */
    int lo = clo32((*psw & MASK_PSW_CDC) << (32 - 7));
    int mask = (1u << (7 - lo)) - 1;
    int count = *psw & mask;
    if (count == 0) {
        return 1;
    }
    (*psw)--;
    return 0;
}

static bool cdc_zero(target_ulong *psw)
{
    int cdc = *psw & MASK_PSW_CDC;
    /* Returns TRUE if PSW.CDC.COUNT == 0 or if PSW.CDC ==
       7'b1111111, otherwise returns FALSE. */
    if (cdc == 0x7f) {
        return true;
    }
    /* find CDC.COUNT */
    int lo = clo32((*psw & MASK_PSW_CDC) << (32 - 7));
    int mask = (1u << (7 - lo)) - 1;
    int count = *psw & mask;
    return count == 0;
}

static void save_context_upper(CPUTriCoreState *env, int ea)
{
    cpu_stl_data(env, ea, env->PCXI);
    cpu_stl_data(env, ea+4, psw_read(env));
    cpu_stl_data(env, ea+8, env->gpr_a[10]);
    cpu_stl_data(env, ea+12, env->gpr_a[11]);
    cpu_stl_data(env, ea+16, env->gpr_d[8]);
    cpu_stl_data(env, ea+20, env->gpr_d[9]);
    cpu_stl_data(env, ea+24, env->gpr_d[10]);
    cpu_stl_data(env, ea+28, env->gpr_d[11]);
    cpu_stl_data(env, ea+32, env->gpr_a[12]);
    cpu_stl_data(env, ea+36, env->gpr_a[13]);
    cpu_stl_data(env, ea+40, env->gpr_a[14]);
    cpu_stl_data(env, ea+44, env->gpr_a[15]);
    cpu_stl_data(env, ea+48, env->gpr_d[12]);
    cpu_stl_data(env, ea+52, env->gpr_d[13]);
    cpu_stl_data(env, ea+56, env->gpr_d[14]);
    cpu_stl_data(env, ea+60, env->gpr_d[15]);
}

static void save_context_lower(CPUTriCoreState *env, int ea)
{
    cpu_stl_data(env, ea, env->PCXI);
    cpu_stl_data(env, ea+4, env->gpr_a[11]);
    cpu_stl_data(env, ea+8, env->gpr_a[2]);
    cpu_stl_data(env, ea+12, env->gpr_a[3]);
    cpu_stl_data(env, ea+16, env->gpr_d[0]);
    cpu_stl_data(env, ea+20, env->gpr_d[1]);
    cpu_stl_data(env, ea+24, env->gpr_d[2]);
    cpu_stl_data(env, ea+28, env->gpr_d[3]);
    cpu_stl_data(env, ea+32, env->gpr_a[4]);
    cpu_stl_data(env, ea+36, env->gpr_a[5]);
    cpu_stl_data(env, ea+40, env->gpr_a[6]);
    cpu_stl_data(env, ea+44, env->gpr_a[7]);
    cpu_stl_data(env, ea+48, env->gpr_d[4]);
    cpu_stl_data(env, ea+52, env->gpr_d[5]);
    cpu_stl_data(env, ea+56, env->gpr_d[6]);
    cpu_stl_data(env, ea+60, env->gpr_d[7]);
}

static void restore_context_upper(CPUTriCoreState *env, int ea,
                                  target_ulong *new_PCXI, target_ulong *new_PSW)
{
    *new_PCXI = cpu_ldl_data(env, ea);
    *new_PSW = cpu_ldl_data(env, ea+4);
    env->gpr_a[10] = cpu_ldl_data(env, ea+8);
    env->gpr_a[11] = cpu_ldl_data(env, ea+12);
    env->gpr_d[8]  = cpu_ldl_data(env, ea+16);
    env->gpr_d[9]  = cpu_ldl_data(env, ea+20);
    env->gpr_d[10] = cpu_ldl_data(env, ea+24);
    env->gpr_d[11] = cpu_ldl_data(env, ea+28);
    env->gpr_a[12] = cpu_ldl_data(env, ea+32);
    env->gpr_a[13] = cpu_ldl_data(env, ea+36);
    env->gpr_a[14] = cpu_ldl_data(env, ea+40);
    env->gpr_a[15] = cpu_ldl_data(env, ea+44);
    env->gpr_d[12] = cpu_ldl_data(env, ea+48);
    env->gpr_d[13] = cpu_ldl_data(env, ea+52);
    env->gpr_d[14] = cpu_ldl_data(env, ea+56);
    env->gpr_d[15] = cpu_ldl_data(env, ea+60);
}

static void restore_context_lower(CPUTriCoreState *env, int ea,
                                  target_ulong *ra, target_ulong *pcxi)
{
    *pcxi = cpu_ldl_data(env, ea);
    *ra = cpu_ldl_data(env, ea+4);
    env->gpr_a[2] = cpu_ldl_data(env, ea+8);
    env->gpr_a[3] = cpu_ldl_data(env, ea+12);
    env->gpr_d[0] = cpu_ldl_data(env, ea+16);
    env->gpr_d[1] = cpu_ldl_data(env, ea+20);
    env->gpr_d[2] = cpu_ldl_data(env, ea+24);
    env->gpr_d[3] = cpu_ldl_data(env, ea+28);
    env->gpr_a[4] = cpu_ldl_data(env, ea+32);
    env->gpr_a[5] = cpu_ldl_data(env, ea+36);
    env->gpr_a[6] = cpu_ldl_data(env, ea+40);
    env->gpr_a[7] = cpu_ldl_data(env, ea+44);
    env->gpr_d[4] = cpu_ldl_data(env, ea+48);
    env->gpr_d[5] = cpu_ldl_data(env, ea+52);
    env->gpr_d[6] = cpu_ldl_data(env, ea+56);
    env->gpr_d[7] = cpu_ldl_data(env, ea+60);
}

void helper_call(CPUTriCoreState *env, uint32_t next_pc)
{
    target_ulong tmp_FCX;
    target_ulong ea;
    target_ulong new_FCX;
    target_ulong psw;

    psw = psw_read(env);
    /* if (FCX == 0) trap(FCU); */
    if (env->FCX == 0) {
        /* FCU trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_FCU, GETPC());
    }
    /* if (PSW.CDE) then if (cdc_increment()) then trap(CDO); */
    if (psw & MASK_PSW_CDE) {
        if (cdc_increment(&psw)) {
            /* CDO trap */
            raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_CDO, GETPC());
        }
    }
    /* PSW.CDE = 1;*/
    psw |= MASK_PSW_CDE;
    /* tmp_FCX = FCX; */
    tmp_FCX = env->FCX;
    /* EA = {FCX.FCXS, 6'b0, FCX.FCXO, 6'b0}; */
    ea = ((env->FCX & MASK_FCX_FCXS) << 12) +
         ((env->FCX & MASK_FCX_FCXO) << 6);
    /* new_FCX = M(EA, word); */
    new_FCX = cpu_ldl_data(env, ea);
    /* M(EA, 16 * word) = {PCXI, PSW, A[10], A[11], D[8], D[9], D[10], D[11],
                           A[12], A[13], A[14], A[15], D[12], D[13], D[14],
                           D[15]}; */
    save_context_upper(env, ea);

    /* PCXI.PCPN = ICR.CCPN; */
    env->PCXI = (env->PCXI & 0xffffff) +
                ((env->ICR & MASK_ICR_CCPN) << 24);
    /* PCXI.PIE = ICR.IE; */
    env->PCXI = ((env->PCXI & ~MASK_PCXI_PIE) +
                ((env->ICR & MASK_ICR_IE) << 15));
    /* PCXI.UL = 1; */
    env->PCXI |= MASK_PCXI_UL;

    /* PCXI[19: 0] = FCX[19: 0]; */
    env->PCXI = (env->PCXI & 0xfff00000) + (env->FCX & 0xfffff);
    /* FCX[19: 0] = new_FCX[19: 0]; */
    env->FCX = (env->FCX & 0xfff00000) + (new_FCX & 0xfffff);
    /* A[11] = next_pc[31: 0]; */
    env->gpr_a[11] = next_pc;

    /* if (tmp_FCX == LCX) trap(FCD);*/
    if (tmp_FCX == env->LCX) {
        /* FCD trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_FCD, GETPC());
    }
    psw_write(env, psw);
}

void helper_ret(CPUTriCoreState *env)
{
    target_ulong ea;
    target_ulong new_PCXI;
    target_ulong new_PSW, psw;

    psw = psw_read(env);
     /* if (PSW.CDE) then if (cdc_decrement()) then trap(CDU);*/
    if (psw & MASK_PSW_CDE) {
        if (cdc_decrement(&psw)) {
            /* CDU trap */
            psw_write(env, psw);
            raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_CDU, GETPC());
        }
    }
    /*   if (PCXI[19: 0] == 0) then trap(CSU); */
    if ((env->PCXI & 0xfffff) == 0) {
        /* CSU trap */
        psw_write(env, psw);
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_CSU, GETPC());
    }
    /* if (PCXI.UL == 0) then trap(CTYP); */
    if ((env->PCXI & MASK_PCXI_UL) == 0) {
        /* CTYP trap */
        cdc_increment(&psw); /* restore to the start of helper */
        psw_write(env, psw);
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_CTYP, GETPC());
    }
    /* PC = {A11 [31: 1], 1’b0}; */
    env->PC = env->gpr_a[11] & 0xfffffffe;

    /* EA = {PCXI.PCXS, 6'b0, PCXI.PCXO, 6'b0}; */
    ea = ((env->PCXI & MASK_PCXI_PCXS) << 12) +
         ((env->PCXI & MASK_PCXI_PCXO) << 6);
    /* {new_PCXI, new_PSW, A[10], A[11], D[8], D[9], D[10], D[11], A[12],
        A[13], A[14], A[15], D[12], D[13], D[14], D[15]} = M(EA, 16 * word); */
    restore_context_upper(env, ea, &new_PCXI, &new_PSW);
    /* M(EA, word) = FCX; */
    cpu_stl_data(env, ea, env->FCX);
    /* FCX[19: 0] = PCXI[19: 0]; */
    env->FCX = (env->FCX & 0xfff00000) + (env->PCXI & 0x000fffff);
    /* PCXI = new_PCXI; */
    env->PCXI = new_PCXI;

    if (tricore_feature(env, TRICORE_FEATURE_13)) {
        /* PSW = new_PSW */
        psw_write(env, new_PSW);
    } else {
        /* PSW = {new_PSW[31:26], PSW[25:24], new_PSW[23:0]}; */
        psw_write(env, (new_PSW & ~(0x3000000)) + (psw & (0x3000000)));
    }
}

void helper_bisr(CPUTriCoreState *env, uint32_t const9)
{
    target_ulong tmp_FCX;
    target_ulong ea;
    target_ulong new_FCX;

    if (env->FCX == 0) {
        /* FCU trap */
       raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_FCU, GETPC());
    }

    tmp_FCX = env->FCX;
    ea = ((env->FCX & 0xf0000) << 12) + ((env->FCX & 0xffff) << 6);

    /* new_FCX = M(EA, word); */
    new_FCX = cpu_ldl_data(env, ea);
    /* M(EA, 16 * word) = {PCXI, A[11], A[2], A[3], D[0], D[1], D[2], D[3], A[4]
                           , A[5], A[6], A[7], D[4], D[5], D[6], D[7]}; */
    save_context_lower(env, ea);


    /* PCXI.PCPN = ICR.CCPN */
    env->PCXI = (env->PCXI & 0xffffff) +
                 ((env->ICR & MASK_ICR_CCPN) << 24);
    /* PCXI.PIE  = ICR.IE */
    env->PCXI = ((env->PCXI & ~MASK_PCXI_PIE) +
                 ((env->ICR & MASK_ICR_IE) << 15));
    /* PCXI.UL = 0 */
    env->PCXI &= ~(MASK_PCXI_UL);
    /* PCXI[19: 0] = FCX[19: 0] */
    env->PCXI = (env->PCXI & 0xfff00000) + (env->FCX & 0xfffff);
    /* FXC[19: 0] = new_FCX[19: 0] */
    env->FCX = (env->FCX & 0xfff00000) + (new_FCX & 0xfffff);
    /* ICR.IE = 1 */
    env->ICR |= MASK_ICR_IE;

    env->ICR |= const9; /* ICR.CCPN = const9[7: 0];*/

    if (tmp_FCX == env->LCX) {
        /* FCD trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_FCD, GETPC());
    }
}

void helper_rfe(CPUTriCoreState *env)
{
    target_ulong ea;
    target_ulong new_PCXI;
    target_ulong new_PSW;
    /* if (PCXI[19: 0] == 0) then trap(CSU); */
    if ((env->PCXI & 0xfffff) == 0) {
        /* raise csu trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_CSU, GETPC());
    }
    /* if (PCXI.UL == 0) then trap(CTYP); */
    if ((env->PCXI & MASK_PCXI_UL) == 0) {
        /* raise CTYP trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_CTYP, GETPC());
    }
    /* if (!cdc_zero() AND PSW.CDE) then trap(NEST); */
    if (!cdc_zero(&(env->PSW)) && (env->PSW & MASK_PSW_CDE)) {
        /* raise NEST trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_NEST, GETPC());
    }
    env->PC = env->gpr_a[11] & ~0x1;
    /* ICR.IE = PCXI.PIE; */
    env->ICR = (env->ICR & ~MASK_ICR_IE) + ((env->PCXI & MASK_PCXI_PIE) >> 15);
    /* ICR.CCPN = PCXI.PCPN; */
    env->ICR = (env->ICR & ~MASK_ICR_CCPN) +
               ((env->PCXI & MASK_PCXI_PCPN) >> 24);
    /*EA = {PCXI.PCXS, 6'b0, PCXI.PCXO, 6'b0};*/
    ea = ((env->PCXI & MASK_PCXI_PCXS) << 12) +
         ((env->PCXI & MASK_PCXI_PCXO) << 6);
    /*{new_PCXI, PSW, A[10], A[11], D[8], D[9], D[10], D[11], A[12],
      A[13], A[14], A[15], D[12], D[13], D[14], D[15]} = M(EA, 16 * word); */
    restore_context_upper(env, ea, &new_PCXI, &new_PSW);
    /* M(EA, word) = FCX;*/
    cpu_stl_data(env, ea, env->FCX);
    /* FCX[19: 0] = PCXI[19: 0]; */
    env->FCX = (env->FCX & 0xfff00000) + (env->PCXI & 0x000fffff);
    /* PCXI = new_PCXI; */
    env->PCXI = new_PCXI;
    /* write psw */
    psw_write(env, new_PSW);
}

void helper_rfm(CPUTriCoreState *env)
{
    env->PC = (env->gpr_a[11] & ~0x1);
    /* ICR.IE = PCXI.PIE; */
    env->ICR = (env->ICR & ~MASK_ICR_IE) |
               ((env->PCXI & MASK_PCXI_PIE) >> 15);
    /* ICR.CCPN = PCXI.PCPN; */
    env->ICR = (env->ICR & ~MASK_ICR_CCPN) |
               ((env->PCXI & MASK_PCXI_PCPN) >> 24);
    /* {PCXI, PSW, A[10], A[11]} = M(DCX, 4 * word); */
    env->PCXI = cpu_ldl_data(env, env->DCX);
    psw_write(env, cpu_ldl_data(env, env->DCX+4));
    env->gpr_a[10] = cpu_ldl_data(env, env->DCX+8);
    env->gpr_a[11] = cpu_ldl_data(env, env->DCX+12);

    if (tricore_feature(env, TRICORE_FEATURE_131)) {
        env->DBGTCR = 0;
    }
}

void helper_ldlcx(CPUTriCoreState *env, uint32_t ea)
{
    uint32_t dummy;
    /* insn doesn't load PCXI and RA */
    restore_context_lower(env, ea, &dummy, &dummy);
}

void helper_lducx(CPUTriCoreState *env, uint32_t ea)
{
    uint32_t dummy;
    /* insn doesn't load PCXI and PSW */
    restore_context_upper(env, ea, &dummy, &dummy);
}

void helper_stlcx(CPUTriCoreState *env, uint32_t ea)
{
    save_context_lower(env, ea);
}

void helper_stucx(CPUTriCoreState *env, uint32_t ea)
{
    save_context_upper(env, ea);
}

void helper_svlcx(CPUTriCoreState *env)
{
    target_ulong tmp_FCX;
    target_ulong ea;
    target_ulong new_FCX;

    if (env->FCX == 0) {
        /* FCU trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_FCU, GETPC());
    }
    /* tmp_FCX = FCX; */
    tmp_FCX = env->FCX;
    /* EA = {FCX.FCXS, 6'b0, FCX.FCXO, 6'b0}; */
    ea = ((env->FCX & MASK_FCX_FCXS) << 12) +
         ((env->FCX & MASK_FCX_FCXO) << 6);
    /* new_FCX = M(EA, word); */
    new_FCX = cpu_ldl_data(env, ea);
    /* M(EA, 16 * word) = {PCXI, PSW, A[10], A[11], D[8], D[9], D[10], D[11],
                           A[12], A[13], A[14], A[15], D[12], D[13], D[14],
                           D[15]}; */
    save_context_lower(env, ea);

    /* PCXI.PCPN = ICR.CCPN; */
    env->PCXI = (env->PCXI & 0xffffff) +
                ((env->ICR & MASK_ICR_CCPN) << 24);
    /* PCXI.PIE = ICR.IE; */
    env->PCXI = ((env->PCXI & ~MASK_PCXI_PIE) +
                ((env->ICR & MASK_ICR_IE) << 15));
    /* PCXI.UL = 0; */
    env->PCXI &= ~MASK_PCXI_UL;

    /* PCXI[19: 0] = FCX[19: 0]; */
    env->PCXI = (env->PCXI & 0xfff00000) + (env->FCX & 0xfffff);
    /* FCX[19: 0] = new_FCX[19: 0]; */
    env->FCX = (env->FCX & 0xfff00000) + (new_FCX & 0xfffff);

    /* if (tmp_FCX == LCX) trap(FCD);*/
    if (tmp_FCX == env->LCX) {
        /* FCD trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_FCD, GETPC());
    }
}

void helper_svucx(CPUTriCoreState *env)
{
    target_ulong tmp_FCX;
    target_ulong ea;
    target_ulong new_FCX;

    if (env->FCX == 0) {
        /* FCU trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_FCU, GETPC());
    }
    /* tmp_FCX = FCX; */
    tmp_FCX = env->FCX;
    /* EA = {FCX.FCXS, 6'b0, FCX.FCXO, 6'b0}; */
    ea = ((env->FCX & MASK_FCX_FCXS) << 12) +
         ((env->FCX & MASK_FCX_FCXO) << 6);
    /* new_FCX = M(EA, word); */
    new_FCX = cpu_ldl_data(env, ea);
    /* M(EA, 16 * word) = {PCXI, PSW, A[10], A[11], D[8], D[9], D[10], D[11],
                           A[12], A[13], A[14], A[15], D[12], D[13], D[14],
                           D[15]}; */
    save_context_upper(env, ea);

    /* PCXI.PCPN = ICR.CCPN; */
    env->PCXI = (env->PCXI & 0xffffff) +
                ((env->ICR & MASK_ICR_CCPN) << 24);
    /* PCXI.PIE = ICR.IE; */
    env->PCXI = ((env->PCXI & ~MASK_PCXI_PIE) +
                ((env->ICR & MASK_ICR_IE) << 15));
    /* PCXI.UL = 1; */
    env->PCXI |= MASK_PCXI_UL;

    /* PCXI[19: 0] = FCX[19: 0]; */
    env->PCXI = (env->PCXI & 0xfff00000) + (env->FCX & 0xfffff);
    /* FCX[19: 0] = new_FCX[19: 0]; */
    env->FCX = (env->FCX & 0xfff00000) + (new_FCX & 0xfffff);

    /* if (tmp_FCX == LCX) trap(FCD);*/
    if (tmp_FCX == env->LCX) {
        /* FCD trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_FCD, GETPC());
    }
}

void helper_rslcx(CPUTriCoreState *env)
{
    target_ulong ea;
    target_ulong new_PCXI;
    /*   if (PCXI[19: 0] == 0) then trap(CSU); */
    if ((env->PCXI & 0xfffff) == 0) {
        /* CSU trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_CSU, GETPC());
    }
    /* if (PCXI.UL == 1) then trap(CTYP); */
    if ((env->PCXI & MASK_PCXI_UL) != 0) {
        /* CTYP trap */
        raise_exception_sync_helper(env, TRAPC_CTX_MNG, TIN3_CTYP, GETPC());
    }
    /* EA = {PCXI.PCXS, 6'b0, PCXI.PCXO, 6'b0}; */
    ea = ((env->PCXI & MASK_PCXI_PCXS) << 12) +
         ((env->PCXI & MASK_PCXI_PCXO) << 6);
    /* {new_PCXI, A[11], A[10], A[11], D[8], D[9], D[10], D[11], A[12],
        A[13], A[14], A[15], D[12], D[13], D[14], D[15]} = M(EA, 16 * word); */
    restore_context_lower(env, ea, &env->gpr_a[11], &new_PCXI);
    /* M(EA, word) = FCX; */
    cpu_stl_data(env, ea, env->FCX);
    /* M(EA, word) = FCX; */
    cpu_stl_data(env, ea, env->FCX);
    /* FCX[19: 0] = PCXI[19: 0]; */
    env->FCX = (env->FCX & 0xfff00000) + (env->PCXI & 0x000fffff);
    /* PCXI = new_PCXI; */
    env->PCXI = new_PCXI;
}

void helper_psw_write(CPUTriCoreState *env, uint32_t arg)
{
    psw_write(env, arg);
}

uint32_t helper_psw_read(CPUTriCoreState *env)
{
    return psw_read(env);
}


static inline void QEMU_NORETURN do_raise_exception_err(CPUTriCoreState *env,
                                                        uint32_t exception,
                                                        int error_code,
                                                        uintptr_t pc)
{
    CPUState *cs = CPU(tricore_env_get_cpu(env));
    cs->exception_index = exception;
    env->error_code = error_code;

    if (pc) {
        /* now we have a real cpu fault */
        cpu_restore_state(cs, pc);
    }

    cpu_loop_exit(cs);
}

void tlb_fill(CPUState *cs, target_ulong addr, int is_write, int mmu_idx,
              uintptr_t retaddr)
{
    int ret;
    ret = cpu_tricore_handle_mmu_fault(cs, addr, is_write, mmu_idx);
    if (ret) {
        TriCoreCPU *cpu = TRICORE_CPU(cs);
        CPUTriCoreState *env = &cpu->env;
        do_raise_exception_err(env, cs->exception_index,
                               env->error_code, retaddr);
    }
}
