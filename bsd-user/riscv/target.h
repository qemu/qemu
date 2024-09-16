/*
 * Riscv64 general target stuff that's common to all aarch details
 *
 * Copyright (c) 2022 M. Warner Losh <imp@bsdimp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TARGET_H
#define TARGET_H

/*
 * riscv64 ABI does not 'lump' the registers for 64-bit args.
 */
static inline bool regpairs_aligned(void *cpu_env)
{
    return false;
}

#endif /* TARGET_H */
