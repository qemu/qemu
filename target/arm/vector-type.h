/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef TARGET_ARM_VECTOR_TYPE_H
#define TARGET_ARM_VECTOR_TYPE_H

/*
 * Define a maximum sized vector register.
 * For 32-bit, this is a 128-bit NEON/AdvSIMD register.
 * For 64-bit, this is a 2048-bit SVE register.
 *
 * Note that the mapping between S, D, and Q views of the register bank
 * differs between AArch64 and AArch32.
 * In AArch32:
 *  Qn = regs[n].d[1]:regs[n].d[0]
 *  Dn = regs[n / 2].d[n & 1]
 *  Sn = regs[n / 4].d[n % 4 / 2],
 *       bits 31..0 for even n, and bits 63..32 for odd n
 *       (and regs[16] to regs[31] are inaccessible)
 * In AArch64:
 *  Zn = regs[n].d[*]
 *  Qn = regs[n].d[1]:regs[n].d[0]
 *  Dn = regs[n].d[0]
 *  Sn = regs[n].d[0] bits 31..0
 *  Hn = regs[n].d[0] bits 15..0
 *
 * This corresponds to the architecturally defined mapping between
 * the two execution states, and means we do not need to explicitly
 * map these registers when changing states.
 *
 * Align the data for use with TCG host vector operations.
 */

#define ARM_MAX_VQ    16

typedef struct ARMVectorReg {
    uint64_t d[2 * ARM_MAX_VQ] QEMU_ALIGNED(16);
} ARMVectorReg;

/* In AArch32 mode, predicate registers do not exist at all.  */
typedef struct ARMPredicateReg {
    uint64_t p[DIV_ROUND_UP(2 * ARM_MAX_VQ, 8)] QEMU_ALIGNED(16);
} ARMPredicateReg;

#endif
