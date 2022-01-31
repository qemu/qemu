/*
 * Intel general target stuff that's common to all i386 details
 *
 * Copyright (c) 2022 M. Warner Losh <imp@bsdimp.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef TARGET_H
#define TARGET_H

/*
 * arm EABI 'lumps' the registers for 64-bit args.
 */
static inline bool regpairs_aligned(void *cpu_env)
{
    return true;
}

#endif /* ! TARGET_H */

