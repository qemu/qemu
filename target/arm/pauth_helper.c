/*
 * ARM v8.3-PAuth Operations
 *
 * Copyright (c) 2019 Linaro, Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
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
#include "qemu/xxhash.h"


static uint64_t pac_cell_shuffle(uint64_t i)
{
    uint64_t o = 0;

    o |= extract64(i, 52, 4);
    o |= extract64(i, 24, 4) << 4;
    o |= extract64(i, 44, 4) << 8;
    o |= extract64(i,  0, 4) << 12;

    o |= extract64(i, 28, 4) << 16;
    o |= extract64(i, 48, 4) << 20;
    o |= extract64(i,  4, 4) << 24;
    o |= extract64(i, 40, 4) << 28;

    o |= extract64(i, 32, 4) << 32;
    o |= extract64(i, 12, 4) << 36;
    o |= extract64(i, 56, 4) << 40;
    o |= extract64(i, 20, 4) << 44;

    o |= extract64(i,  8, 4) << 48;
    o |= extract64(i, 36, 4) << 52;
    o |= extract64(i, 16, 4) << 56;
    o |= extract64(i, 60, 4) << 60;

    return o;
}

static uint64_t pac_cell_inv_shuffle(uint64_t i)
{
    uint64_t o = 0;

    o |= extract64(i, 12, 4);
    o |= extract64(i, 24, 4) << 4;
    o |= extract64(i, 48, 4) << 8;
    o |= extract64(i, 36, 4) << 12;

    o |= extract64(i, 56, 4) << 16;
    o |= extract64(i, 44, 4) << 20;
    o |= extract64(i,  4, 4) << 24;
    o |= extract64(i, 16, 4) << 28;

    o |= i & MAKE_64BIT_MASK(32, 4);
    o |= extract64(i, 52, 4) << 36;
    o |= extract64(i, 28, 4) << 40;
    o |= extract64(i,  8, 4) << 44;

    o |= extract64(i, 20, 4) << 48;
    o |= extract64(i,  0, 4) << 52;
    o |= extract64(i, 40, 4) << 56;
    o |= i & MAKE_64BIT_MASK(60, 4);

    return o;
}

static uint64_t pac_sub(uint64_t i)
{
    static const uint8_t sub[16] = {
        0xb, 0x6, 0x8, 0xf, 0xc, 0x0, 0x9, 0xe,
        0x3, 0x7, 0x4, 0x5, 0xd, 0x2, 0x1, 0xa,
    };
    uint64_t o = 0;
    int b;

    for (b = 0; b < 64; b += 4) {
        o |= (uint64_t)sub[(i >> b) & 0xf] << b;
    }
    return o;
}

static uint64_t pac_inv_sub(uint64_t i)
{
    static const uint8_t inv_sub[16] = {
        0x5, 0xe, 0xd, 0x8, 0xa, 0xb, 0x1, 0x9,
        0x2, 0x6, 0xf, 0x0, 0x4, 0xc, 0x7, 0x3,
    };
    uint64_t o = 0;
    int b;

    for (b = 0; b < 64; b += 4) {
        o |= (uint64_t)inv_sub[(i >> b) & 0xf] << b;
    }
    return o;
}

static int rot_cell(int cell, int n)
{
    /* 4-bit rotate left by n.  */
    cell |= cell << 4;
    return extract32(cell, 4 - n, 4);
}

static uint64_t pac_mult(uint64_t i)
{
    uint64_t o = 0;
    int b;

    for (b = 0; b < 4 * 4; b += 4) {
        int i0, i4, i8, ic, t0, t1, t2, t3;

        i0 = extract64(i, b, 4);
        i4 = extract64(i, b + 4 * 4, 4);
        i8 = extract64(i, b + 8 * 4, 4);
        ic = extract64(i, b + 12 * 4, 4);

        t0 = rot_cell(i8, 1) ^ rot_cell(i4, 2) ^ rot_cell(i0, 1);
        t1 = rot_cell(ic, 1) ^ rot_cell(i4, 1) ^ rot_cell(i0, 2);
        t2 = rot_cell(ic, 2) ^ rot_cell(i8, 1) ^ rot_cell(i0, 1);
        t3 = rot_cell(ic, 1) ^ rot_cell(i8, 2) ^ rot_cell(i4, 1);

        o |= (uint64_t)t3 << b;
        o |= (uint64_t)t2 << (b + 4 * 4);
        o |= (uint64_t)t1 << (b + 8 * 4);
        o |= (uint64_t)t0 << (b + 12 * 4);
    }
    return o;
}

static uint64_t tweak_cell_rot(uint64_t cell)
{
    return (cell >> 1) | (((cell ^ (cell >> 1)) & 1) << 3);
}

static uint64_t tweak_shuffle(uint64_t i)
{
    uint64_t o = 0;

    o |= extract64(i, 16, 4) << 0;
    o |= extract64(i, 20, 4) << 4;
    o |= tweak_cell_rot(extract64(i, 24, 4)) << 8;
    o |= extract64(i, 28, 4) << 12;

    o |= tweak_cell_rot(extract64(i, 44, 4)) << 16;
    o |= extract64(i,  8, 4) << 20;
    o |= extract64(i, 12, 4) << 24;
    o |= tweak_cell_rot(extract64(i, 32, 4)) << 28;

    o |= extract64(i, 48, 4) << 32;
    o |= extract64(i, 52, 4) << 36;
    o |= extract64(i, 56, 4) << 40;
    o |= tweak_cell_rot(extract64(i, 60, 4)) << 44;

    o |= tweak_cell_rot(extract64(i,  0, 4)) << 48;
    o |= extract64(i,  4, 4) << 52;
    o |= tweak_cell_rot(extract64(i, 40, 4)) << 56;
    o |= tweak_cell_rot(extract64(i, 36, 4)) << 60;

    return o;
}

static uint64_t tweak_cell_inv_rot(uint64_t cell)
{
    return ((cell << 1) & 0xf) | ((cell & 1) ^ (cell >> 3));
}

static uint64_t tweak_inv_shuffle(uint64_t i)
{
    uint64_t o = 0;

    o |= tweak_cell_inv_rot(extract64(i, 48, 4));
    o |= extract64(i, 52, 4) << 4;
    o |= extract64(i, 20, 4) << 8;
    o |= extract64(i, 24, 4) << 12;

    o |= extract64(i,  0, 4) << 16;
    o |= extract64(i,  4, 4) << 20;
    o |= tweak_cell_inv_rot(extract64(i,  8, 4)) << 24;
    o |= extract64(i, 12, 4) << 28;

    o |= tweak_cell_inv_rot(extract64(i, 28, 4)) << 32;
    o |= tweak_cell_inv_rot(extract64(i, 60, 4)) << 36;
    o |= tweak_cell_inv_rot(extract64(i, 56, 4)) << 40;
    o |= tweak_cell_inv_rot(extract64(i, 16, 4)) << 44;

    o |= extract64(i, 32, 4) << 48;
    o |= extract64(i, 36, 4) << 52;
    o |= extract64(i, 40, 4) << 56;
    o |= tweak_cell_inv_rot(extract64(i, 44, 4)) << 60;

    return o;
}

static uint64_t pauth_computepac_architected(uint64_t data, uint64_t modifier,
                                             ARMPACKey key)
{
    static const uint64_t RC[5] = {
        0x0000000000000000ull,
        0x13198A2E03707344ull,
        0xA4093822299F31D0ull,
        0x082EFA98EC4E6C89ull,
        0x452821E638D01377ull,
    };
    const uint64_t alpha = 0xC0AC29B7C97C50DDull;
    /*
     * Note that in the ARM pseudocode, key0 contains bits <127:64>
     * and key1 contains bits <63:0> of the 128-bit key.
     */
    uint64_t key0 = key.hi, key1 = key.lo;
    uint64_t workingval, runningmod, roundkey, modk0;
    int i;

    modk0 = (key0 << 63) | ((key0 >> 1) ^ (key0 >> 63));
    runningmod = modifier;
    workingval = data ^ key0;

    for (i = 0; i <= 4; ++i) {
        roundkey = key1 ^ runningmod;
        workingval ^= roundkey;
        workingval ^= RC[i];
        if (i > 0) {
            workingval = pac_cell_shuffle(workingval);
            workingval = pac_mult(workingval);
        }
        workingval = pac_sub(workingval);
        runningmod = tweak_shuffle(runningmod);
    }
    roundkey = modk0 ^ runningmod;
    workingval ^= roundkey;
    workingval = pac_cell_shuffle(workingval);
    workingval = pac_mult(workingval);
    workingval = pac_sub(workingval);
    workingval = pac_cell_shuffle(workingval);
    workingval = pac_mult(workingval);
    workingval ^= key1;
    workingval = pac_cell_inv_shuffle(workingval);
    workingval = pac_inv_sub(workingval);
    workingval = pac_mult(workingval);
    workingval = pac_cell_inv_shuffle(workingval);
    workingval ^= key0;
    workingval ^= runningmod;
    for (i = 0; i <= 4; ++i) {
        workingval = pac_inv_sub(workingval);
        if (i < 4) {
            workingval = pac_mult(workingval);
            workingval = pac_cell_inv_shuffle(workingval);
        }
        runningmod = tweak_inv_shuffle(runningmod);
        roundkey = key1 ^ runningmod;
        workingval ^= RC[4 - i];
        workingval ^= roundkey;
        workingval ^= alpha;
    }
    workingval ^= modk0;

    return workingval;
}

static uint64_t pauth_computepac_impdef(uint64_t data, uint64_t modifier,
                                        ARMPACKey key)
{
    return qemu_xxhash64_4(data, modifier, key.lo, key.hi);
}

static uint64_t pauth_computepac(CPUARMState *env, uint64_t data,
                                 uint64_t modifier, ARMPACKey key)
{
    if (cpu_isar_feature(aa64_pauth_arch, env_archcpu(env))) {
        return pauth_computepac_architected(data, modifier, key);
    } else {
        return pauth_computepac_impdef(data, modifier, key);
    }
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

    pac = pauth_computepac(env, ext_ptr, modifier, *key);

    /*
     * Check if the ptr has good extension bits and corrupt the
     * pointer authentication code if not.
     */
    test = sextract64(ptr, bot_bit, top_bit - bot_bit);
    if (test != 0 && test != -1) {
        /*
         * Note that our top_bit is one greater than the pseudocode's
         * version, hence "- 2" here.
         */
        pac ^= MAKE_64BIT_MASK(top_bit - 2, 1);
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
    /* Note that bit 55 is used whether or not the regime has 2 ranges. */
    uint64_t extfield = sextract64(ptr, 55, 1);
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
    pac = pauth_computepac(env, orig_ptr, modifier, *key);
    bot_bit = 64 - param.tsz;
    top_bit = 64 - 8 * param.tbi;

    test = (pac ^ ptr) & ~MAKE_64BIT_MASK(55, 1);
    if (unlikely(extract64(test, bot_bit, top_bit - bot_bit))) {
        int error_code = (keynumber << 1) | (keynumber ^ 1);
        if (param.tbi) {
            return deposit64(orig_ptr, 53, 2, error_code);
        } else {
            return deposit64(orig_ptr, 61, 2, error_code);
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
    if (el < 2 && arm_is_el2_enabled(env)) {
        uint64_t hcr = arm_hcr_el2_eff(env);
        bool trap = !(hcr & HCR_API);
        if (el == 0) {
            /* Trap only applies to EL1&0 regime.  */
            trap &= (hcr & (HCR_E2H | HCR_TGE)) != (HCR_E2H | HCR_TGE);
        }
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
    return (arm_sctlr(env, el) & bit) != 0;
}

uint64_t HELPER(pacia)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnIA)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_addpac(env, x, y, &env->keys.apia, false);
}

uint64_t HELPER(pacib)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnIB)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_addpac(env, x, y, &env->keys.apib, false);
}

uint64_t HELPER(pacda)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnDA)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_addpac(env, x, y, &env->keys.apda, true);
}

uint64_t HELPER(pacdb)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnDB)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_addpac(env, x, y, &env->keys.apdb, true);
}

uint64_t HELPER(pacga)(CPUARMState *env, uint64_t x, uint64_t y)
{
    uint64_t pac;

    pauth_check_trap(env, arm_current_el(env), GETPC());
    pac = pauth_computepac(env, x, y, env->keys.apga);

    return pac & 0xffffffff00000000ull;
}

uint64_t HELPER(autia)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnIA)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_auth(env, x, y, &env->keys.apia, false, 0);
}

uint64_t HELPER(autib)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnIB)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_auth(env, x, y, &env->keys.apib, false, 1);
}

uint64_t HELPER(autda)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnDA)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_auth(env, x, y, &env->keys.apda, true, 0);
}

uint64_t HELPER(autdb)(CPUARMState *env, uint64_t x, uint64_t y)
{
    int el = arm_current_el(env);
    if (!pauth_key_enabled(env, el, SCTLR_EnDB)) {
        return x;
    }
    pauth_check_trap(env, el, GETPC());
    return pauth_auth(env, x, y, &env->keys.apdb, true, 1);
}

uint64_t HELPER(xpaci)(CPUARMState *env, uint64_t a)
{
    return pauth_strip(env, a, false);
}

uint64_t HELPER(xpacd)(CPUARMState *env, uint64_t a)
{
    return pauth_strip(env, a, true);
}
