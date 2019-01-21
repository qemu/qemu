/*
 * ARM v8.3-PAuth Operations
 *
 * Copyright (c) 2019 Linaro, Ltd.
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
#include "internals.h"
#include "exec/exec-all.h"
#include "exec/cpu_ldst.h"
#include "exec/helper-proto.h"
#include "tcg/tcg-gvec-desc.h"


static uint64_t pauth_computepac(uint64_t data, uint64_t modifier,
                                 ARMPACKey key)
{
    g_assert_not_reached(); /* FIXME */
}

static uint64_t pauth_addpac(CPUARMState *env, uint64_t ptr, uint64_t modifier,
                             ARMPACKey *key, bool data)
{
    ARMMMUIdx mmu_idx = arm_stage1_mmu_idx(env);
    ARMVAParameters param = aa64_va_parameters(env, ptr, mmu_idx, data);
    uint64_t pac, ext_ptr, ext, test;
    int bot_bit, top_bit;

    /* If tagged pointers are in use, use ptr<55>, otherwise ptr<63>.  */
    if (param.tbi) {
        ext = sextract64(ptr, 55, 1);
    } else {
        ext = sextract64(ptr, 63, 1);
    }

    /* Build a pointer with known good extension bits.  */
    top_bit = 64 - 8 * param.tbi;
    bot_bit = 64 - param.tsz;
    ext_ptr = deposit64(ptr, bot_bit, top_bit - bot_bit, ext);

    pac = pauth_computepac(ext_ptr, modifier, *key);

    /*
     * Check if the ptr has good extension bits and corrupt the
     * pointer authentication code if not.
     */
    test = sextract64(ptr, bot_bit, top_bit - bot_bit);
    if (test != 0 && test != -1) {
        pac ^= MAKE_64BIT_MASK(top_bit - 1, 1);
    }

    /*
     * Preserve the determination between upper and lower at bit 55,
     * and insert pointer authentication code.
     */
    if (param.tbi) {
        ptr &= ~MAKE_64BIT_MASK(bot_bit, 55 - bot_bit + 1);
        pac &= MAKE_64BIT_MASK(bot_bit, 54 - bot_bit + 1);
    } else {
        ptr &= MAKE_64BIT_MASK(0, bot_bit);
        pac &= ~(MAKE_64BIT_MASK(55, 1) | MAKE_64BIT_MASK(0, bot_bit));
    }
    ext &= MAKE_64BIT_MASK(55, 1);
    return pac | ext | ptr;
}

static uint64_t pauth_original_ptr(uint64_t ptr, ARMVAParameters param)
{
    uint64_t extfield = -param.select;
    int bot_pac_bit = 64 - param.tsz;
    int top_pac_bit = 64 - 8 * param.tbi;

    return deposit64(ptr, bot_pac_bit, top_pac_bit - bot_pac_bit, extfield);
}

static uint64_t pauth_auth(CPUARMState *env, uint64_t ptr, uint64_t modifier,
                           ARMPACKey *key, bool data, int keynumber)
{
    ARMMMUIdx mmu_idx = arm_stage1_mmu_idx(env);
    ARMVAParameters param = aa64_va_parameters(env, ptr, mmu_idx, data);
    int bot_bit, top_bit;
    uint64_t pac, orig_ptr, test;

    orig_ptr = pauth_original_ptr(ptr, param);
    pac = pauth_computepac(orig_ptr, modifier, *key);
    bot_bit = 64 - param.tsz;
    top_bit = 64 - 8 * param.tbi;

    test = (pac ^ ptr) & ~MAKE_64BIT_MASK(55, 1);
    if (unlikely(extract64(test, bot_bit, top_bit - bot_bit))) {
        int error_code = (keynumber << 1) | (keynumber ^ 1);
        if (param.tbi) {
            return deposit64(ptr, 53, 2, error_code);
        } else {
            return deposit64(ptr, 61, 2, error_code);
        }
    }
    return orig_ptr;
}

static uint64_t pauth_strip(CPUARMState *env, uint64_t ptr, bool data)
{
    ARMMMUIdx mmu_idx = arm_stage1_mmu_idx(env);
    ARMVAParameters param = aa64_va_parameters(env, ptr, mmu_idx, data);

    return pauth_original_ptr(ptr, param);
}

static void QEMU_NORETURN pauth_trap(CPUARMState *env, int target_el,
                                     uintptr_t ra)
{
    raise_exception_ra(env, EXCP_UDEF, syn_pactrap(), target_el, ra);
}

static void pauth_check_trap(CPUARMState *env, int el, uintptr_t ra)
{
    if (el < 2 && arm_feature(env, ARM_FEATURE_EL2)) {
        uint64_t hcr = arm_hcr_el2_eff(env);
        bool trap = !(hcr & HCR_API);
        /* FIXME: ARMv8.1-VHE: trap only applies to EL1&0 regime.  */
        /* FIXME: ARMv8.3-NV: HCR_NV trap takes precedence for ERETA[AB].  */
        if (trap) {
            pauth_trap(env, 2, ra);
        }
    }
    if (el < 3 && arm_feature(env, ARM_FEATURE_EL3)) {
        if (!(env->cp15.scr_el3 & SCR_API)) {
            pauth_trap(env, 3, ra);
        }
    }
}

static bool pauth_key_enabled(CPUARMState *env, int el, uint32_t bit)
{
    uint32_t sctlr;
    if (el == 0) {
        /* FIXME: ARMv8.1-VHE S2 translation regime.  */
        sctlr = env->cp15.sctlr_el[1];
    } else {
        sctlr = env->cp15.sctlr_el[el];
    }
    return (sctlr & bit) != 0;
}

uint64_t HELPER(pacia)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnIA)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_addpac(env, x, y, &env->apia_key, false);
}

uint64_t HELPER(pacib)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnIB)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_addpac(env, x, y, &env->apib_key, false);
}

uint64_t HELPER(pacda)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnDA)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_addpac(env, x, y, &env->apda_key, true);
}

uint64_t HELPER(pacdb)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnDB)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_addpac(env, x, y, &env->apdb_key, true);
}

uint64_t HELPER(pacga)(CPUARMState *env, uint64_t x, uint64_t y)
{
    uint64_t pac;

    pauth_check_trap(env, arm_current_el(env), GETPC());
    pac = pauth_computepac(x, y, env->apga_key);

    return pac & 0xffffffff00000000ull;
}

uint64_t HELPER(autia)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnIA)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_auth(env, x, y, &env->apia_key, false, 0);
}

uint64_t HELPER(autib)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnIB)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_auth(env, x, y, &env->apib_key, false, 1);
}

uint64_t HELPER(autda)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnDA)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_auth(env, x, y, &env->apda_key, true, 0);
}

uint64_t HELPER(autdb)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnDB)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_auth(env, x, y, &env->apdb_key, true, 1);
}

uint64_t HELPER(xpaci)(CPUARMState *env, uint64_t a)
{
    return pauth_strip(env, a, false);
}

uint64_t HELPER(xpacd)(CPUARMState *env, uint64_t a)
{
    return pauth_strip(env, a, true);
}
