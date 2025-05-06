/*
 *  S/390 integer helper routines
 *
 *  Copyright (c) 2009 Ulrich Hecht
 *  Copyright (c) 2009 Alexander Graf
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
#include "s390x-internal.h"
#include "tcg_s390x.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"

/* #define DEBUG_HELPER */
#ifdef DEBUG_HELPER
#define HELPER_LOG(x...) qemu_log(x)
#else
#define HELPER_LOG(x...)
#endif

/* 64/32 -> 32 signed division */
uint64_t HELPER(divs32)(CPUS390XState *env, int64_t a, int64_t b64)
{
    int32_t b = b64;
    int64_t q, r;

    if (b == 0) {
        tcg_s390_program_interrupt(env, PGM_FIXPT_DIVIDE, GETPC());
    }

    q = a / b;
    r = a % b;

    /* Catch non-representable quotient.  */
    if (q != (int32_t)q) {
        tcg_s390_program_interrupt(env, PGM_FIXPT_DIVIDE, GETPC());
    }

    return deposit64(q, 32, 32, r);
}

/* 64/32 -> 32 unsigned division */
uint64_t HELPER(divu32)(CPUS390XState *env, uint64_t a, uint64_t b64)
{
    uint32_t b = b64;
    uint64_t q, r;

    if (b == 0) {
        tcg_s390_program_interrupt(env, PGM_FIXPT_DIVIDE, GETPC());
    }

    q = a / b;
    r = a % b;

    /* Catch non-representable quotient.  */
    if (q != (uint32_t)q) {
        tcg_s390_program_interrupt(env, PGM_FIXPT_DIVIDE, GETPC());
    }

    return deposit64(q, 32, 32, r);
}

/* 64/64 -> 64 signed division */
Int128 HELPER(divs64)(CPUS390XState *env, int64_t a, int64_t b)
{
    /* Catch divide by zero, and non-representable quotient (MIN / -1).  */
    if (b == 0 || (b == -1 && a == (1ll << 63))) {
        tcg_s390_program_interrupt(env, PGM_FIXPT_DIVIDE, GETPC());
    }
    return int128_make128(a / b, a % b);
}

/* 128 -> 64/64 unsigned division */
Int128 HELPER(divu64)(CPUS390XState *env, uint64_t ah, uint64_t al, uint64_t b)
{
    if (b != 0) {
        uint64_t r = divu128(&al, &ah, b);
        if (ah == 0) {
            return int128_make128(al, r);
        }
    }
    /* divide by zero or overflow */
    tcg_s390_program_interrupt(env, PGM_FIXPT_DIVIDE, GETPC());
}

void HELPER(cvb)(CPUS390XState *env, uint32_t r1, uint64_t dec)
{
    int64_t pow10 = 1, bin = 0;
    int digit, sign;

    sign = dec & 0xf;
    if (sign < 0xa) {
        tcg_s390_data_exception(env, 0, GETPC());
    }
    dec >>= 4;

    while (dec) {
        digit = dec & 0xf;
        if (digit > 0x9) {
            tcg_s390_data_exception(env, 0, GETPC());
        }
        dec >>= 4;
        bin += digit * pow10;
        pow10 *= 10;
    }

    if (sign == 0xb || sign == 0xd) {
        bin = -bin;
    }

    /* R1 is updated even on fixed-point-divide exception. */
    env->regs[r1] = (env->regs[r1] & 0xffffffff00000000ULL) | (uint32_t)bin;
    if (bin != (int32_t)bin) {
        tcg_s390_program_interrupt(env, PGM_FIXPT_DIVIDE, GETPC());
    }
}

uint64_t HELPER(cvbg)(CPUS390XState *env, Int128 dec)
{
    uint64_t dec64[] = {int128_getlo(dec), int128_gethi(dec)};
    int64_t bin = 0, pow10, tmp;
    int digit, i, sign;

    sign = dec64[0] & 0xf;
    if (sign < 0xa) {
        tcg_s390_data_exception(env, 0, GETPC());
    }
    dec64[0] >>= 4;
    pow10 = (sign == 0xb || sign == 0xd) ? -1 : 1;

    for (i = 1; i < 20; i++) {
        digit = dec64[i >> 4] & 0xf;
        if (digit > 0x9) {
            tcg_s390_data_exception(env, 0, GETPC());
        }
        dec64[i >> 4] >>= 4;
        /*
         * Prepend the next digit and check for overflow. The multiplication
         * cannot overflow, since, conveniently, the int64_t limits are
         * approximately +-9.2E+18. If bin is zero, the addition cannot
         * overflow. Otherwise bin is known to have the same sign as the rhs
         * addend, in which case overflow happens if and only if the result
         * has a different sign.
         */
        tmp = bin + pow10 * digit;
        if (bin && ((tmp ^ bin) < 0)) {
            tcg_s390_program_interrupt(env, PGM_FIXPT_DIVIDE, GETPC());
        }
        bin = tmp;
        pow10 *= 10;
    }

    g_assert(!dec64[0]);
    if (dec64[1]) {
        tcg_s390_program_interrupt(env, PGM_FIXPT_DIVIDE, GETPC());
    }

    return bin;
}

uint64_t HELPER(cvd)(int32_t reg)
{
    /* positive 0 */
    uint64_t dec = 0x0c;
    int64_t bin = reg;
    int shift;

    if (bin < 0) {
        bin = -bin;
        dec = 0x0d;
    }

    for (shift = 4; (shift < 64) && bin; shift += 4) {
        dec |= (bin % 10) << shift;
        bin /= 10;
    }

    return dec;
}

Int128 HELPER(cvdg)(int64_t reg)
{
    /* positive 0 */
    Int128 dec = int128_make64(0x0c);
    Int128 bin = int128_makes64(reg);
    Int128 base = int128_make64(10);
    int shift;

    if (!int128_nonneg(bin)) {
        bin = int128_neg(bin);
        dec = int128_make64(0x0d);
    }

    for (shift = 4; (shift < 128) && int128_nz(bin); shift += 4) {
        dec = int128_or(dec, int128_lshift(int128_remu(bin, base), shift));
        bin = int128_divu(bin, base);
    }

    return dec;
}

uint64_t HELPER(popcnt)(uint64_t val)
{
    /* Note that we don't fold past bytes. */
    val = (val & 0x5555555555555555ULL) + ((val >> 1) & 0x5555555555555555ULL);
    val = (val & 0x3333333333333333ULL) + ((val >> 2) & 0x3333333333333333ULL);
    val = (val + (val >> 4)) & 0x0f0f0f0f0f0f0f0fULL;
    return val;
}
