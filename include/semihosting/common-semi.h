/*
 *  Semihosting support for systems modeled on the Arm "Angel"
 *  semihosting syscalls design. This includes Arm and RISC-V processors
 *
 *  Copyright (c) 2005, 2007 CodeSourcery.
 *  Copyright (c) 2019 Linaro
 *  Written by Paul Brook.
 *
 *  Copyright © 2020 by Keith Packard <keithp@keithp.com>
 *  Adapted for systems other than ARM, including RISC-V, by Keith Packard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  ARM Semihosting is documented in:
 *     Semihosting for AArch32 and AArch64 Release 2.0
 *     https://static.docs.arm.com/100863/0200/semihosting.pdf
 *
 *  RISC-V Semihosting is documented in:
 *     RISC-V Semihosting
 *     https://github.com/riscv/riscv-semihosting-spec/blob/main/riscv-semihosting-spec.adoc
 */

#ifndef COMMON_SEMI_H
#define COMMON_SEMI_H

typedef struct SemihostingCPUOps {
    /* Return argument @argno */
    uint64_t (*arg)(const CPUState *cs, int argno);
    /* Set return value for semihosting call */
    void (*set_ret)(CPUState *cs, uint64_t ret);
    /* Set an error for a semihosting call */
    void (*set_err)(CPUState *cs, int err);
    /* True if the guest is using the 64-bit semihosting ABI */
    bool (*is_64bit)(const CPUState *cs);
    /* True if SYS_EXIT takes an extended parameter block */
    bool (*sys_exit_is_extended)(const CPUState *cs);
    /* Return lowest address until which stack can grow */
    uint64_t (*stack_bottom)(const CPUState *cs);
    /* True if SYS_SYNCCACHE is implemented */
    bool (*has_synccache)(const CPUState *cs);
} SemihostingCPUOps;

void common_semi_cb(CPUState *cs, uint64_t ret, int err);
void do_common_semihosting(CPUState *cs);

#endif /* COMMON_SEMI_H */
